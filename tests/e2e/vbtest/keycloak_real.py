"""A real Keycloak behind the `IdpBackend` interface (`--vb-idp=keycloak`, K5).

Two ways to get one, stdlib only:

* `VB_KEYCLOAK_URL=http://127.0.0.1:8180` (plus `VB_KEYCLOAK_ADMIN` / `VB_KEYCLOAK_ADMIN_PASSWORD`,
  default admin/admin): use a Keycloak you started yourself, with realm `e2e` imported from
  tests/e2e/fixtures/keycloak/realm-e2e.json (`kc.sh start-dev --import-realm`).
* otherwise `docker run` the pinned image (`VB_KEYCLOAK_IMAGE`, default `PINNED_IMAGE`) with that realm
  mounted, published on 127.0.0.1 only (the engine allows `http://` issuers only on loopback), and
  removed when the run ends.

Clients (the engine, the browser the tests play) reach Keycloak through `RecordingProxy`, which
forwards unchanged, keeps the same request log as the emulator (`requests()`, `grants()`), and gives
Keycloak the proxy's `Host`, so its issuer is `http://127.0.0.1:<proxy port>/realms/e2e`. Admin
operations go straight to Keycloak's admin REST API.

The realm is reset between tests (`reset()`); options the real realm cannot express raise
`NotSupported`, which skips the test.
"""
import http.client
import json
import os
import pathlib
import shutil
import subprocess
import threading
import time
import urllib.error
import urllib.parse
import urllib.request
import uuid
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

from .idp import IdpBackend, NotSupported

PINNED_IMAGE = "quay.io/keycloak/keycloak:26.7.5"
REALM_FILE = pathlib.Path(__file__).resolve().parents[1] / "fixtures" / "keycloak" / "realm-e2e.json"
SECRET_FORM_KEYS = {"code", "code_verifier", "refresh_token", "client_secret", "password", "id_token",
                    "access_token", "token", "id_token_hint"}
HOP_BY_HOP = {"connection", "keep-alive", "proxy-authenticate", "proxy-authorization", "te", "trailers",
              "transfer-encoding", "upgrade", "content-length", "host"}
REALM_DEFAULTS = {"accessTokenLifespan": 300, "ssoSessionIdleTimeout": 1800, "ssoSessionMaxLifespan": 36000,
                  "revokeRefreshToken": False, "defaultSignatureAlgorithm": "RS256"}
REALM_SETTINGS = {"sso_session_idle_timeout": "ssoSessionIdleTimeout", "sso_session_max_lifespan": "ssoSessionMaxLifespan",
                  "access_token_lifespan": "accessTokenLifespan", "revoke_refresh_token": "revokeRefreshToken"}


def _endpoint(path, realm):
    base = "/realms/%s/" % realm
    table = {".well-known/openid-configuration": "discovery", "protocol/openid-connect/certs": "certs",
             "protocol/openid-connect/auth": "auth", "login-actions/authenticate": "login",
             "protocol/openid-connect/token": "token", "protocol/openid-connect/logout": "logout",
             "protocol/openid-connect/userinfo": "userinfo", "protocol/openid-connect/token/introspect": "introspect"}
    return table.get(path[len(base):]) if path.startswith(base) else None


def _short(value):
    value = str(value)
    return value[:4] + "..." if len(value) > 4 else value


class RecordingProxy:
    """Forwards every request to `upstream` unchanged and records it (same entries as MockKeycloak)."""

    def __init__(self, upstream_host, upstream_port, realm):
        self.upstream, self.realm = (upstream_host, upstream_port), realm
        self.log, self.lock = [], threading.Lock()
        proxy = self

        class Handler(BaseHTTPRequestHandler):
            protocol_version = "HTTP/1.0"

            def log_message(self, *a):
                pass

            def _forward(self):
                proxy._forward(self)

            do_GET = do_POST = do_PUT = do_DELETE = do_HEAD = do_OPTIONS = _forward

        self._server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        self.port = self._server.server_address[1]
        threading.Thread(target=self._server.serve_forever, kwargs={"poll_interval": 0.02}, daemon=True).start()

    def _forward(self, h):
        url = urllib.parse.urlparse(h.path)
        body = h.rfile.read(int(h.headers.get("Content-Length", "0") or 0))
        params = dict(urllib.parse.parse_qsl(url.query, keep_blank_values=True))
        if body and "form" in h.headers.get("Content-Type", ""):
            params.update(dict(urllib.parse.parse_qsl(body.decode("utf-8", "replace"), keep_blank_values=True)))
        entry = {"t": round(time.time(), 3), "method": h.command, "path": url.path,
                 "endpoint": _endpoint(url.path, self.realm),
                 "params": {k: (_short(v) if k in SECRET_FORM_KEYS else v) for k, v in params.items()},
                 "status": None, "fault": None, "_raw": dict(params)}
        with self.lock:
            self.log.append(entry)
        headers = {k: v for k, v in h.headers.items() if k.lower() not in HOP_BY_HOP}
        headers["Host"] = h.headers.get("Host", "127.0.0.1:%d" % self.port)  # Keycloak derives its issuer from this
        try:
            conn = http.client.HTTPConnection(*self.upstream, timeout=60)
            conn.request(h.command, h.path, body or None, headers)
            resp = conn.getresponse()
            data = resp.read()
        except OSError as e:
            entry["status"] = "upstream-error"
            h.send_error(502, str(e))
            return
        entry["status"] = resp.status
        h.send_response(resp.status)
        for k, v in resp.getheaders():  # getheaders keeps repeated Set-Cookie lines
            if k.lower() not in HOP_BY_HOP:
                h.send_header(k, v)
        h.send_header("Content-Length", str(len(data)))
        h.end_headers()
        if h.command != "HEAD":
            h.wfile.write(data)

    def stop(self):
        self._server.shutdown()
        self._server.server_close()


class RealKeycloak(IdpBackend):
    client_id = "vb-e2e"
    realm = "e2e"

    def __init__(self, base_url, admin_user="admin", admin_password="admin", container=None):
        parsed = urllib.parse.urlparse(base_url)
        self.base = base_url.rstrip("/")
        self.admin_user, self.admin_password, self.container = admin_user, admin_password, container
        self.proxy = RecordingProxy(parsed.hostname, parsed.port, self.realm)
        self.issuer = "http://127.0.0.1:%d/realms/%s" % (self.proxy.port, self.realm)
        self.local_url = self.issuer
        self._token = (None, 0)
        self._realm_id = None
        self._users = {}                  # username -> id
        self._roles, self._groups = set(), set()
        self._mappers = {}                # name -> id for the client's protocol mappers we touch
        self.interactive = True
        self._client_uuid = None

    # -- admin REST ------------------------------------------------------------------------
    def _admin_token(self):
        token, expires = self._token
        if token and time.time() < expires - 10:
            return token
        data = urllib.parse.urlencode({"grant_type": "password", "client_id": "admin-cli", "username": self.admin_user,
                                       "password": self.admin_password}).encode()
        with urllib.request.urlopen(urllib.request.Request(self.base + "/realms/master/protocol/openid-connect/token",
                                                           data=data), timeout=30) as r:
            body = json.loads(r.read())
        self._token = (body["access_token"], time.time() + body.get("expires_in", 60))
        return self._token[0]

    def admin(self, method, path, body=None, ok=(200, 201, 204)):
        """One admin REST call; returns (status, json or None, headers)."""
        data = json.dumps(body).encode() if body is not None else None
        req = urllib.request.Request(self.base + "/admin/realms/" + self.realm + path, data=data, method=method,
                                     headers={"Authorization": "Bearer " + self._admin_token(),
                                              "Content-Type": "application/json"})
        try:
            with urllib.request.urlopen(req, timeout=60) as r:
                raw, status, headers = r.read(), r.status, dict(r.headers)
        except urllib.error.HTTPError as e:
            raw, status, headers = e.read(), e.code, dict(e.headers)
        if status not in ok:
            raise RuntimeError("Keycloak admin %s %s -> %d %s" % (method, path, status, raw[:300]))
        return status, (json.loads(raw) if raw else None), headers

    def _realm(self):
        return self.admin("GET", "")[1]

    def _client(self):
        if self._client_uuid is None:
            self._client_uuid = self.admin("GET", "/clients?clientId=" + self.client_id)[1][0]["id"]
        return self._client_uuid

    # -- IdpBackend: users and admin operations -------------------------------------------------
    def _find_user(self, username):
        found = self.admin("GET", "/users?exact=true&username=" + urllib.parse.quote(username))[1]
        return found[0]["id"] if found else None

    def user_id(self, username):
        uid = self._users.get(username) or self._find_user(username)
        if uid is None:
            raise KeyError("no such user %r (add_user first)" % username)
        return uid

    def add_user(self, username, password, *, email=None, groups=(), roles=(), enabled=True,
                 email_verified=True, name=None, claims=None):
        if claims:
            raise NotSupported("extra claims (add a mapper to the realm export)")
        rep = {"username": username, "enabled": enabled, "emailVerified": email_verified,
               "email": email if email is not None else username + "@example.com",
               "firstName": (name or username.capitalize()), "lastName": "E2E",
               "credentials": [{"type": "password", "value": password, "temporary": False}]}
        uid = self._find_user(username)
        if uid is None:
            _, _, headers = self.admin("POST", "/users", rep)
            uid = headers["Location"].rsplit("/", 1)[1]
        else:
            self.admin("PUT", "/users/" + uid, {k: v for k, v in rep.items() if k != "credentials"})
            self.admin("PUT", "/users/%s/reset-password" % uid, rep["credentials"][0])
        self._users[username] = uid
        self.set_groups(username, groups)
        self.set_roles(username, roles)
        return uid

    def disable_user(self, username):
        self.admin("PUT", "/users/" + self.user_id(username), {"enabled": False})

    def enable_user(self, username):
        self.admin("PUT", "/users/" + self.user_id(username), {"enabled": True})

    def admin_logout(self, username):
        self.admin("POST", "/users/%s/logout" % self.user_id(username))

    def _group_id(self, name):
        found = [g for g in self.admin("GET", "/groups?exact=true&search=" + urllib.parse.quote(name))[1]
                 if g["name"] == name]
        if found:
            return found[0]["id"]
        _, _, headers = self.admin("POST", "/groups", {"name": name})
        self._groups.add(name)
        return headers["Location"].rsplit("/", 1)[1]

    def set_groups(self, username, groups):
        uid = self.user_id(username)
        want = {g.strip("/") for g in groups}
        have = {g["name"]: g["id"] for g in self.admin("GET", "/users/%s/groups" % uid)[1]}
        for name, gid in have.items():
            if name not in want:
                self.admin("DELETE", "/users/%s/groups/%s" % (uid, gid))
        for name in want - set(have):
            self.admin("PUT", "/users/%s/groups/%s" % (uid, self._group_id(name)))

    def _role(self, name):
        status, rep, _ = self.admin("GET", "/roles/" + urllib.parse.quote(name), ok=(200, 404))
        if status == 404:
            self.admin("POST", "/roles", {"name": name})
            self._roles.add(name)
            rep = self.admin("GET", "/roles/" + urllib.parse.quote(name))[1]
        return rep

    def set_roles(self, username, roles):
        uid = self.user_id(username)
        mapped = self.admin("GET", "/users/%s/role-mappings/realm" % uid)[1]
        keep = {"default-roles-" + self.realm, "offline_access", "uma_authorization"}
        drop = [r for r in mapped if r["name"] not in set(roles) | keep]
        add = [self._role(r) for r in roles if r not in {m["name"] for m in mapped}]
        if drop:
            self.admin("DELETE", "/users/%s/role-mappings/realm" % uid, drop)
        if add:
            self.admin("POST", "/users/%s/role-mappings/realm" % uid, add)

    def set_realm(self, **settings):
        unknown = set(settings) - set(REALM_SETTINGS)
        if unknown:
            raise NotSupported("realm setting(s) %s" % ", ".join(sorted(unknown)))
        rep = self._realm()
        for k, v in settings.items():
            rep[REALM_SETTINGS[k]] = v if isinstance(v, bool) else int(v)
        self.admin("PUT", "", rep)

    # -- keys -----------------------------------------------------------------------------------
    def _keys(self):
        return self.admin("GET", "/keys")[1]

    @property
    def active_kid(self):
        return self._keys()["active"][self._realm()["defaultSignatureAlgorithm"]]

    def _add_key_provider(self, name, provider_id, priority, config):
        realm_id = self._realm()["id"]
        self.admin("POST", "/components", {
            "name": name, "providerId": provider_id, "providerType": "org.keycloak.keys.KeyProvider",
            "parentId": realm_id, "config": dict({"priority": [str(priority)], "enabled": ["true"], "active": ["true"]},
                                                 **config)})

    def rotate_keys(self):
        n = len([k for k in self._keys()["keys"] if k["algorithm"] == self._realm()["defaultSignatureAlgorithm"]])
        if self._realm()["defaultSignatureAlgorithm"] == "ES256":
            self._add_key_provider("e2e-rotated-%d" % n, "ecdsa-generated", 1000 + n, {"ecdsaEllipticCurveKey": ["P-256"]})
        else:
            self._add_key_provider("e2e-rotated-%d" % n, "rsa-generated", 1000 + n, {"keySize": ["2048"]})
        return self.active_kid

    def retire_key(self, kid):
        if kid == self.active_kid:
            raise ValueError("cannot retire the active key; rotate_keys() first")
        keys = [k for k in self._keys()["keys"] if k["kid"] == kid]
        if not keys:
            raise KeyError("no key %s" % kid)
        self.admin("DELETE", "/components/" + keys[0]["providerId"])

    # -- reset between tests ----------------------------------------------------------------------
    def reset(self, **options):
        supported = {"interactive", "alg", "audience_mapper", "extra_audience", "group_paths", "redirect_pattern",
                     *REALM_SETTINGS, "access_token_lifespan"}
        unsupported = set(options) - supported
        if unsupported:
            raise NotSupported("realm option(s) %s cannot be set on a real Keycloak" % ", ".join(sorted(unsupported)))
        with self.proxy.lock:
            self.proxy.log.clear()
        for u in self.admin("GET", "/users?max=1000")[1]:
            self.admin("DELETE", "/users/" + u["id"])
        self._users.clear()
        for g in self.admin("GET", "/groups?max=1000")[1]:
            self.admin("DELETE", "/groups/" + g["id"])
        for name in list(self._roles):
            self.admin("DELETE", "/roles/" + urllib.parse.quote(name), ok=(200, 204, 404))
        self._roles.clear()
        for comp in self.admin("GET", "/components?type=org.keycloak.keys.KeyProvider")[1]:
            if comp["name"].startswith("e2e-"):
                self.admin("DELETE", "/components/" + comp["id"])
        rep = self._realm()
        rep.update(REALM_DEFAULTS)
        self.admin("PUT", "", rep)
        self.admin("POST", "/logout-all", ok=(200, 204))
        client = self.admin("GET", "/clients/" + self._client())[1]
        client["redirectUris"] = [options.get("redirect_pattern", "http://127.0.0.1/*")]
        client["protocolMappers"] = [m for m in client["protocolMappers"] if m["name"] not in ("audience", "audience-extra")]
        for m in client["protocolMappers"]:
            if m["protocolMapper"] == "oidc-group-membership-mapper":
                m["config"]["full.path"] = "true" if options.get("group_paths") else "false"
        if options.get("audience_mapper"):
            client["protocolMappers"].append({
                "name": "audience", "protocol": "openid-connect", "protocolMapper": "oidc-audience-mapper",
                "consentRequired": False,
                "config": {"included.client.audience": self.client_id, "id.token.claim": "true",
                           "access.token.claim": "true"}})
        if options.get("extra_audience"):
            client["protocolMappers"].append({
                "name": "audience-extra", "protocol": "openid-connect", "protocolMapper": "oidc-audience-mapper",
                "consentRequired": False,
                "config": {"included.custom.audience": options["extra_audience"], "id.token.claim": "true",
                           "access.token.claim": "true"}})
        self.admin("PUT", "/clients/" + self._client(), client)
        settings = {k: v for k, v in options.items() if k in REALM_SETTINGS}
        if settings:
            self.set_realm(**settings)
        if options.get("alg", "RS256") == "ES256":
            self._add_key_provider("e2e-es256", "ecdsa-generated", 2000, {"ecdsaEllipticCurveKey": ["P-256"]})
            rep = self._realm()
            rep["defaultSignatureAlgorithm"] = "ES256"
            self.admin("PUT", "", rep)
        elif options.get("alg", "RS256") != "RS256":
            raise NotSupported("alg %s" % options["alg"])

    # -- the log the proxy keeps -------------------------------------------------------------------
    def request_log(self):
        with self.proxy.lock:
            return [{k: v for k, v in e.items() if k != "_raw"} for e in self.proxy.log]

    def requests(self, endpoint=None, status=None):
        return [e for e in self.request_log() if endpoint in (None, e["endpoint"]) and status in (None, e["status"])]

    def grants(self):
        return [e["params"].get("grant_type") for e in self.requests("token")]

    @property
    def token_requests(self):
        with self.proxy.lock:
            return [dict(e["_raw"]) for e in self.proxy.log if e["endpoint"] == "token"]

    @property
    def auth_requests(self):
        with self.proxy.lock:
            return [dict(e["_raw"]) for e in self.proxy.log if e["endpoint"] == "auth"]

    def stop(self):
        self.proxy.stop()
        if self.container:
            subprocess.run(["docker", "rm", "-f", self.container], capture_output=True)


def _wait_ready(base_url, timeout=240):
    deadline = time.time() + timeout
    url = base_url + "/realms/e2e/.well-known/openid-configuration"
    while time.time() < deadline:
        try:
            with urllib.request.urlopen(url, timeout=5) as r:
                if r.status == 200:
                    return
        except (urllib.error.URLError, OSError):
            pass
        time.sleep(1)
    raise TimeoutError("Keycloak did not become ready at %s within %ds (realm e2e imported?)" % (base_url, timeout))


def start(config=None):
    """The session's real Keycloak: VB_KEYCLOAK_URL if set, else a pinned docker container."""
    admin = (os.environ.get("VB_KEYCLOAK_ADMIN", "admin"), os.environ.get("VB_KEYCLOAK_ADMIN_PASSWORD", "admin"))
    url = os.environ.get("VB_KEYCLOAK_URL")
    container = None
    if not url:
        if not shutil.which("docker"):
            raise RuntimeError("--vb-idp=keycloak needs docker, or VB_KEYCLOAK_URL pointing at a Keycloak with "
                               "realm e2e imported from %s" % REALM_FILE)
        container = "vb-e2e-keycloak-" + uuid.uuid4().hex[:8]
        image = os.environ.get("VB_KEYCLOAK_IMAGE", PINNED_IMAGE)
        run = subprocess.run(
            ["docker", "run", "-d", "--rm", "--name", container, "-p", "127.0.0.1::8080",
             "-e", "KC_BOOTSTRAP_ADMIN_USERNAME=%s" % admin[0], "-e", "KC_BOOTSTRAP_ADMIN_PASSWORD=%s" % admin[1],
             "-v", "%s:/opt/keycloak/data/import/realm-e2e.json:ro" % REALM_FILE, image,
             "start-dev", "--import-realm"], capture_output=True, text=True)
        if run.returncode:
            raise RuntimeError("docker run failed (image pull problem? that is infrastructure, not a test "
                               "failure): %s" % run.stderr.strip())
        port = subprocess.run(["docker", "port", container, "8080/tcp"], capture_output=True, text=True).stdout
        url = "http://127.0.0.1:%s" % port.strip().splitlines()[0].rsplit(":", 1)[1]
    try:
        _wait_ready(url)
        backend = RealKeycloak(url, admin[0], admin[1], container)
        backend.admin("GET", "")  # the admin credentials work
        return backend
    except BaseException:
        if container:
            subprocess.run(["docker", "rm", "-f", container], capture_output=True)
        raise
