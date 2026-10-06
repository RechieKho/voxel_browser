"""A Keycloak emulator for the auth e2e tests (stdlib only).

Faithful where the engine's auth code can tell the difference: Keycloak's URL layout, discovery
document, ID-token claims (`azp`, `typ`, `sid`, `auth_time`, `realm_access.roles`, `aud` arrays,
full-path groups), error bodies, SSO sessions with idle/max lifespans, refresh-token rotation,
RS256 and ES256 signing with key rotation, an optional real login form, and fault injection.
Tokens are signed with the TEST-ONLY keys in tests/e2e/fixtures/ by pure Python (vbtest/jwscrypto.py),
so the harness still needs nothing but pytest.

    idp = MockKeycloak().start()
    idp.add_user("alice", "pw", groups=["admins"], roles=["moderator"])
    tokens = idp.obtain_tokens("alice", "pw")            # a real code-flow sign-in
    token = idp.mint("sub-x", "x", azp="other")          # a *bad* token, field by field
    idp.faults.status("certs", 503, times=3)             # the IdP misbehaves
    idp.issuer, idp.client_id                            # put these in the pack's auth.lua

What it is NOT: it does not prove the engine works against Keycloak. tests/e2e/
test_mock_keycloak_matches_real.py compares it against a real Keycloak so it cannot drift.
Everything it knows about Keycloak's wording is in this file, marked `# KC:`.
"""
import base64
import hashlib
import html
import http.cookiejar
import http.cookies
import json
import pathlib
import random
import re
import secrets
import socket
import threading
import time
import urllib.error
import urllib.parse
import urllib.request
import uuid
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

from . import jwscrypto as jc
from .idp import IdpBackend, LoginRefused

FIXTURES = pathlib.Path(__file__).resolve().parents[1] / "fixtures"
SECRET_FORM_KEYS = {"code", "code_verifier", "refresh_token", "client_secret", "password", "id_token",
                    "access_token", "token", "id_token_hint"}
ENDPOINTS = ("discovery", "certs", "auth", "login", "token", "logout", "userinfo", "introspect")
b64u = jc.b64u


def shorten(value):
    value = str(value)
    return value[:4] + "..." if len(value) > 4 else value


class Key:
    """One realm signing key. `alg` is RS256 or ES256."""

    def __init__(self, kid, alg, **material):
        self.kid, self.alg, self.m = kid, alg, material

    @classmethod
    def load(cls, filename):
        raw = json.loads((FIXTURES / filename).read_text())
        if "crv" in raw:
            d = int(raw["d"], 16)
            return cls(raw["kid"], "ES256", d=d, point=(int(raw["x"], 16), int(raw["y"], 16)))
        return cls(raw["kid"], "RS256", n=int(raw["n"], 16), e=int(raw["e"], 16), d=int(raw["d"], 16))

    @classmethod
    def random_ec(cls, kid):
        d = secrets.randbelow(jc.N - 1) + 1
        return cls(kid, "ES256", d=d, point=jc.public_point(d))

    def sign(self, signing_input):
        if self.alg == "ES256":
            return jc.es256_sign(signing_input, self.m["d"])
        return jc.rs256_sign(signing_input, self.m["n"], self.m["d"])

    def verify(self, signing_input, signature):
        if self.alg == "ES256":
            return jc.es256_verify(signing_input, signature, self.m["point"])
        return jc.rs256_verify(signing_input, signature, self.m["n"], self.m["e"])

    def jwk(self):
        if self.alg == "ES256":
            x, y = self.m["point"]
            return {"kid": self.kid, "kty": "EC", "alg": "ES256", "use": "sig", "crv": "P-256",
                    "x": jc.b64u_int(x, 32), "y": jc.b64u_int(y, 32)}
        return {"kid": self.kid, "kty": "RSA", "alg": "RS256", "use": "sig",
                "n": jc.b64u_int(self.m["n"]), "e": jc.b64u_int(self.m["e"])}


class Faults:
    """Per-endpoint misbehaviour. Endpoints: discovery, certs, auth, login, token, logout,
    userinfo, introspect. `times=None` means until cleared. Every applied fault is recorded in
    the request log, so a failing test says which fault was active.

        idp.faults.latency("token", 5)            # every token request takes 5 s longer
        idp.faults.status("certs", 503, times=3)  # the next 3 JWKS fetches get a 503
        idp.faults.body("certs", b"{")            # malformed (or 2 MiB) body, status 200
        idp.faults.drop("token")                  # close the socket without answering
        idp.faults.discovery_issuer("https://evil/realms/x")
        idp.faults.clear()
    """

    def __init__(self):
        self._lock = threading.Lock()
        self._latency = {}
        self._rules = []
        self.issuer_override = None

    def _check(self, endpoint):
        if endpoint not in ENDPOINTS:
            raise ValueError("unknown endpoint %r (one of %s)" % (endpoint, ", ".join(ENDPOINTS)))

    def latency(self, endpoint, seconds):
        self._check(endpoint)
        with self._lock:
            self._latency[endpoint] = seconds

    def status(self, endpoint, code, times=None, body=None):
        self._add(endpoint, "status", times, code=code, body=body)

    def body(self, endpoint, raw, times=None, status=200):
        self._add(endpoint, "body", times, raw=raw, code=status)

    def drop(self, endpoint, times=None):
        self._add(endpoint, "drop", times)

    def _add(self, endpoint, kind, times, **kw):
        self._check(endpoint)
        with self._lock:
            self._rules.append(dict(kw, endpoint=endpoint, kind=kind, times=times))

    def discovery_issuer(self, other):
        self.issuer_override = other

    def clear(self, endpoint=None):
        with self._lock:
            if endpoint is None:
                self._latency.clear()
                self._rules.clear()
                self.issuer_override = None
            else:
                self._latency.pop(endpoint, None)
                self._rules = [r for r in self._rules if r["endpoint"] != endpoint]

    def take(self, endpoint):
        """(latency seconds, rule or None); consumes one use of the first matching rule."""
        with self._lock:
            delay = self._latency.get(endpoint, 0)
            for rule in self._rules:
                if rule["endpoint"] == endpoint and (rule["times"] is None or rule["times"] > 0):
                    if rule["times"] is not None:
                        rule["times"] -= 1
                    return delay, rule
            return delay, None


class _User:
    def __init__(self, uid, username, password, email, email_verified, enabled, groups, roles, name, extra):
        self.id, self.username, self.password = uid, username, password
        self.email, self.email_verified, self.enabled = email, email_verified, enabled
        self.groups, self.roles, self.name, self.extra_claims = list(groups), list(roles), name, dict(extra)


class _Session:
    def __init__(self, sid, user_id, now):
        self.id, self.user_id, self.created, self.last_refresh, self.active = sid, user_id, now, now, True


class _Redirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, *a, **kw):  # never follow: the caller wants to see the Location
        return None


class MockKeycloak(IdpBackend):
    def __init__(self, client_id="vb-e2e", realm="e2e", alg="RS256", audience_mapper=False,
                 group_paths=False, interactive=False, redirect_pattern="http://127.0.0.1/*",
                 pkce_required=True, sso_session_idle_timeout=1800, sso_session_max_lifespan=36000,
                 access_token_lifespan=300, revoke_refresh_token=False, include_enc_key=True,
                 clock=None, seed=None):
        self.client_id, self.realm, self.alg = client_id, realm, alg
        self.audience_mapper, self.group_paths, self.interactive = audience_mapper, group_paths, interactive
        self.redirect_pattern, self.pkce_required = redirect_pattern, pkce_required
        self.settings = {"sso_session_idle_timeout": sso_session_idle_timeout,
                         "sso_session_max_lifespan": sso_session_max_lifespan,
                         "access_token_lifespan": access_token_lifespan,
                         "revoke_refresh_token": revoke_refresh_token}
        self.include_enc_key = include_enc_key
        self.faults = Faults()
        self._now = clock or time.time
        self._rand = random.Random(seed) if seed is not None else None
        self.lock = threading.RLock()
        self.users = {}            # username -> _User
        self._legacy = {}          # subject -> _User (ad-hoc users from next_user / issue_refresh_token)
        self.sessions = {}         # sid -> _Session
        self.codes = {}            # code -> dict
        self.refresh = {}          # refresh token -> dict
        self.auth_sessions = {}    # session_code -> dict (interactive login in progress)
        self.token_requests = []   # form dicts the token endpoint received, unredacted (for assertions)
        self.auth_requests = []    # query dicts the authorization endpoint received, unredacted
        self._log = []
        # "Who is at the browser" for the non-interactive /auth: a registered username, or an
        # ad-hoc {"subject", "name", "claims"} (what MockIdp offered before).
        self.next_user = {"subject": "sub-browser", "name": "browser-user", "claims": {}}
        self.next_username = None
        self._login_errors = []    # [(error, description, times)] for the next sign-ins
        self._keys = []            # every key ever made, retired ones too (so tests can mint with them)
        self._in_jwks = []         # kids the realm publishes
        self._pool = {"RS256": ["idp_rsa.json", "idp_rsa_2.json"], "ES256": ["idp_ec.json"]}[alg]
        self._ec_n = 0
        self.active_kid = self._new_key().kid
        self._server = None
        self.port = 0

    # -- lifecycle ---------------------------------------------------------------------------
    def start(self):
        idp = self

        class Handler(BaseHTTPRequestHandler):
            def log_message(self, *a):  # keep pytest output clean
                pass

            def do_GET(self):
                idp._serve(self, "GET")

            def do_POST(self):
                idp._serve(self, "POST")

        self._server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        self.port = self._server.server_address[1]
        threading.Thread(target=self._server.serve_forever, kwargs={"poll_interval": 0.02}, daemon=True).start()
        return self

    def stop(self):
        if self._server:
            self._server.shutdown()
            self._server.server_close()
            self._server = None

    @property
    def issuer(self):
        return "http://127.0.0.1:%d/realms/%s" % (self.port, self.realm)

    @property
    def _oidc(self):
        return self.issuer + "/protocol/openid-connect"

    # -- small helpers -------------------------------------------------------------------------
    def now(self):
        return int(self._now())

    def _hex(self, n=8):
        return "%0*x" % (2 * n, self._rand.getrandbits(8 * n)) if self._rand else secrets.token_hex(n)

    def _uuid(self):
        return str(uuid.UUID(int=self._rand.getrandbits(128), version=4)) if self._rand else str(uuid.uuid4())

    def _new_key(self):
        if self.alg == "ES256" and self._ec_n >= len(self._pool):
            key = Key.random_ec("e2e-ec-%d" % (self._ec_n + 1))
        else:
            used = {k.kid for k in self._keys}
            if self.alg == "ES256":
                key = Key.load(self._pool[self._ec_n])
            else:
                fresh = [f for f in self._pool if Key.load(f).kid not in used]
                if not fresh:
                    raise RuntimeError("out of fixture RSA keys (tests/e2e/fixtures/idp_rsa*.json)")
                key = Key.load(fresh[0])
        self._ec_n += 1
        self._keys.append(key)
        self._in_jwks.append(key.kid)
        return key

    def key(self, kid=None):
        kid = kid or self.active_kid
        return next((k for k in self._keys if k.kid == kid), None)

    # -- IdpBackend: admin ---------------------------------------------------------------------
    def add_user(self, username, password, *, email=None, groups=(), roles=(), enabled=True,
                 email_verified=True, name=None, claims=None):
        with self.lock:
            u = _User(self._uuid(), username, password, email if email is not None else username + "@example.com",
                      email_verified, enabled, groups, roles, name or username.capitalize(), claims or {})
            self.users[username] = u
            return u

    def _user_named(self, username):
        try:
            return self.users[username]
        except KeyError:
            raise KeyError("no such user %r (add_user first)" % username) from None

    def _user_by_id(self, uid):
        return next((u for u in list(self.users.values()) + list(self._legacy.values()) if u.id == uid), None)

    def disable_user(self, username):
        self._user_named(username).enabled = False

    def enable_user(self, username):
        self._user_named(username).enabled = True

    def set_groups(self, username, groups):
        self._user_named(username).groups = list(groups)

    def set_roles(self, username, roles):
        self._user_named(username).roles = list(roles)

    def admin_logout(self, username):
        with self.lock:
            uid = self._user_named(username).id
            for s in self.sessions.values():
                if s.user_id == uid:
                    s.active = False

    def set_realm(self, **settings):
        unknown = set(settings) - set(self.settings)
        if unknown:
            raise ValueError("unknown realm setting(s): %s" % ", ".join(sorted(unknown)))
        self.settings.update(settings)

    def rotate_keys(self):
        with self.lock:
            self.active_kid = self._new_key().kid
            return self.active_kid

    def retire_key(self, kid):
        with self.lock:
            if kid == self.active_kid:
                raise ValueError("cannot retire the active key; rotate_keys() first")
            self._in_jwks = [k for k in self._in_jwks if k != kid]

    # Aliases from MockIdp, until test_auth.py is migrated.
    def revoke_all(self, subject=None):
        """An admin ends the user's sessions (instant at the IdP)."""
        with self.lock:
            for s in self.sessions.values():
                if subject in (None, s.user_id):
                    s.active = False

    def set_claims(self, subject, claims):
        """Change what the next token for `subject` carries (e.g. a group was removed)."""
        with self.lock:
            u = self._user_by_id(subject)
            if u:
                u.extra_claims = dict(claims)

    def issue_refresh_token(self, subject, name, claims=None):
        with self.lock:
            sess = _Session("sid-" + self._hex(), self._adhoc_user(subject, name, claims).id, self._now())
            self.sessions[sess.id] = sess
            return self._new_refresh_token(sess)

    def cancel_logins(self, error="access_denied", description="User cancelled", times=1):
        """The next `times` sign-ins end with the IdP redirecting back with `error=...`."""
        self._login_errors.append([error, description, times])

    def login_as(self, username):
        """The non-interactive /auth signs this registered user in next."""
        self._user_named(username)
        self.next_username = username

    # -- claims and tokens ---------------------------------------------------------------------
    def _groups_claim(self, u):
        if self.group_paths:
            return [g if g.startswith("/") else "/" + g for g in u.groups]
        return [g.split("/")[-1] for g in u.groups]

    def _aud(self, access=False):
        if self.audience_mapper:
            return [self.client_id, "account"]
        return "account" if access else self.client_id

    def _payload(self, u, sess, nonce=None, iat=None, access=False, at_hash=None):
        iat = self.now() if iat is None else iat
        life = self.settings["access_token_lifespan"]
        p = {"exp": iat + life, "iat": iat, "auth_time": int(sess.created) if sess else iat,
             "jti": self._uuid(), "iss": self.issuer, "aud": self._aud(access), "sub": u.id,
             "typ": "Bearer" if access else "ID", "azp": self.client_id, "sid": sess.id if sess else self._uuid()}
        if nonce:
            p["nonce"] = nonce  # code flow only: refresh-derived tokens carry none
        if at_hash:
            p["at_hash"] = at_hash
        p["acr"] = "1"
        p["realm_access"] = {"roles": ["default-roles-" + self.realm, "offline_access", "uma_authorization"] + u.roles}
        p["email_verified"] = u.email_verified
        p["name"] = u.name
        p["groups"] = self._groups_claim(u)
        p["preferred_username"] = u.username
        if u.email is not None:
            p["email"] = u.email
        p.update(u.extra_claims)
        return p

    def _jws(self, payload, alg=None, kid=None, header_typ="JWT", tamper=False):
        key = self.key(kid) or self.key()
        header = {"alg": alg or key.alg, "typ": header_typ, "kid": kid or key.kid}
        head = b64u(json.dumps(header, separators=(",", ":")).encode())
        body = b64u(json.dumps(payload, separators=(",", ":")).encode())
        signing_input = (head + "." + body).encode()
        token = head + "." + body + "." + b64u(key.sign(signing_input))
        if tamper:  # change the payload after signing
            forged = b64u(json.dumps(dict(payload, sub="admin"), separators=(",", ":")).encode())
            token = head + "." + forged + "." + token.rsplit(".", 1)[1]
        return token

    def mint(self, subject, name, claims=None, nonce=None, iat=None, exp=None, aud=None, iss=None,
             alg=None, kid=None, tamper=False, azp=None, typ=None, drop=(), sid=None, header_typ="JWT"):
        """An ID token as this realm would issue it. Override fields to make a *bad* one:
        `alg`/`kid` go in the JWS header (the signature is still the active key's), `typ` is the
        payload claim, `header_typ` the JWS header's, `drop` removes claims by name."""
        iat = self.now() if iat is None else iat
        p = {"exp": iat + self.settings["access_token_lifespan"] if exp is None else exp, "iat": iat,
             "auth_time": iat, "jti": self._uuid(), "iss": iss or self.issuer, "aud": aud or self._aud(),
             "sub": subject, "typ": "ID" if typ is None else typ, "azp": azp or self.client_id,
             "sid": sid or self._uuid(), "preferred_username": name}
        if nonce:
            p["nonce"] = nonce
        p.update(claims or {})
        for k in drop:
            p.pop(k, None)
        return self._jws(p, alg=alg, kid=kid, header_typ=header_typ, tamper=tamper)

    def _adhoc_user(self, subject, name, claims):
        u = self._legacy.get(subject)
        if u is None:
            u = _User(subject, name, None, None, True, True, [], [], name, claims or {})
            self._legacy[subject] = u
        u.extra_claims = dict(claims or {})
        return u

    def _new_refresh_token(self, sess):
        rt = "rt-" + self._hex(16)
        self.refresh[rt] = {"sid": sess.id, "used": False}
        return rt

    def _token_response(self, u, sess, nonce, refresh_token):
        now = self.now()
        access = self._jws(self._payload(u, sess, iat=now, access=True))
        at_hash = b64u(hashlib.sha256(access.encode()).digest()[:16])
        id_token = self._jws(self._payload(u, sess, nonce=nonce, iat=now, at_hash=at_hash))
        return {"access_token": access, "expires_in": self.settings["access_token_lifespan"],
                "refresh_expires_in": self.settings["sso_session_idle_timeout"], "refresh_token": refresh_token,
                "token_type": "Bearer", "id_token": id_token, "not-before-policy": 0, "session_state": sess.id,
                "scope": "openid email profile"}

    def jwks(self):
        keys = [k.jwk() for k in self._keys if k.kid in self._in_jwks]
        if self.include_enc_key:  # KC: a realm also publishes an RSA-OAEP encryption key
            n = Key.load("idp_rsa_2.json").m["n"]
            keys.append({"kid": "e2e-enc-1", "kty": "RSA", "alg": "RSA-OAEP", "use": "enc",
                         "n": jc.b64u_int(n), "e": jc.b64u_int(65537)})
        return {"keys": keys}

    def discovery(self):
        o = self._oidc
        return {"issuer": self.faults.issuer_override or self.issuer,
                "authorization_endpoint": o + "/auth", "token_endpoint": o + "/token",
                "introspection_endpoint": o + "/token/introspect", "userinfo_endpoint": o + "/userinfo",
                "end_session_endpoint": o + "/logout", "jwks_uri": o + "/certs",
                "grant_types_supported": ["authorization_code", "implicit", "refresh_token", "password",
                                          "client_credentials"],
                "response_types_supported": ["code", "none", "id_token", "token", "id_token token", "code id_token",
                                             "code token", "code id_token token"],
                "subject_types_supported": ["public", "pairwise"],
                "id_token_signing_alg_values_supported": [self.alg],
                "scopes_supported": ["openid", "email", "profile", "roles"],
                "token_endpoint_auth_methods_supported": ["private_key_jwt", "client_secret_basic",
                                                          "client_secret_post"],
                "code_challenge_methods_supported": ["plain", "S256"],
                "claims_supported": ["aud", "sub", "iss", "auth_time", "name", "given_name", "family_name",
                                     "preferred_username", "email", "acr"]}

    # -- request log -----------------------------------------------------------------------------
    def request_log(self):
        with self.lock:
            return [dict(e) for e in self._log]

    def requests(self, endpoint=None, status=None):
        return [e for e in self.request_log() if endpoint in (None, e["endpoint"]) and status in (None, e["status"])]

    def grants(self):
        """grant_type of every token request, oldest first."""
        return [e["params"].get("grant_type") for e in self.requests("token")]

    # -- HTTP --------------------------------------------------------------------------------
    def _endpoint(self, path):
        base = "/realms/%s/" % self.realm
        table = {".well-known/openid-configuration": "discovery", "protocol/openid-connect/certs": "certs",
                 "protocol/openid-connect/auth": "auth", "login-actions/authenticate": "login",
                 "protocol/openid-connect/token": "token", "protocol/openid-connect/logout": "logout",
                 "protocol/openid-connect/userinfo": "userinfo", "protocol/openid-connect/token/introspect": "introspect"}
        return table.get(path[len(base):]) if path.startswith(base) else None

    def _serve(self, h, method):
        url = urllib.parse.urlparse(h.path)
        query = {k: v[0] for k, v in urllib.parse.parse_qs(url.query, keep_blank_values=True).items()}
        form = {}
        if method == "POST":
            n = int(h.headers.get("Content-Length", "0"))
            form = {k: v[0] for k, v in urllib.parse.parse_qs(h.rfile.read(n).decode(), keep_blank_values=True).items()}
        endpoint = self._endpoint(url.path)
        entry = {"t": round(time.time(), 3), "method": method, "path": url.path, "endpoint": endpoint,
                 "params": {k: (shorten(v) if k in SECRET_FORM_KEYS else v) for k, v in {**query, **form}.items()},
                 "status": None, "fault": None}
        h._entry = entry
        with self.lock:
            self._log.append(entry)
        try:
            if endpoint is None:
                return self._send(h, 404, {"error": "not_found"})
            delay, rule = self.faults.take(endpoint)
            faults = []
            if delay:
                faults.append("latency=%gs" % delay)
                time.sleep(delay)
            if rule:
                faults.append(rule["kind"] + ("=%s" % rule.get("code") if rule["kind"] != "drop" else ""))
                entry["fault"] = ",".join(faults)
                if rule["kind"] == "drop":
                    entry["status"] = "dropped"
                    h.close_connection = True
                    try:
                        h.request.shutdown(socket.SHUT_RDWR)
                    except OSError:
                        pass
                    return
                if rule["kind"] == "status":
                    body = rule["body"] if rule["body"] is not None else {"error": "server_error"}
                    return self._send(h, rule["code"], body)
                return self._send(h, rule["code"], rule["raw"], "application/json")
            entry["fault"] = ",".join(faults) or None
            getattr(self, "_ep_" + endpoint)(h, method, query, form)
        except (BrokenPipeError, ConnectionResetError):
            entry["status"] = entry["status"] or "reset"

    def _send(self, h, status, body, ctype="application/json", headers=None):
        data = body if isinstance(body, bytes) else (body.encode() if isinstance(body, str) else json.dumps(body).encode())
        h._entry["status"] = status
        h.send_response(status)
        h.send_header("Content-Type", ctype)
        h.send_header("Content-Length", str(len(data)))
        for k, v in (headers or {}).items():
            h.send_header(k, v)
        h.end_headers()
        h.wfile.write(data)

    def _error(self, h, status, error, description=None):
        body = {"error": error}
        if description:
            body["error_description"] = description
        self._send(h, status, body)

    def _page(self, h, status, title, message, headers=None):
        self._send(h, status, "<html><head><title>%s</title></head><body><div id=\"kc-error-message\"><h1>%s</h1>"
                   "<p class=\"instruction\">%s</p></div></body></html>" % (title, title, html.escape(message)),
                   "text/html; charset=utf-8", headers)

    def _redirect(self, h, location, headers=None):
        self._send(h, 302, b"", "text/plain", dict(headers or {}, Location=location))

    # -- endpoints ---------------------------------------------------------------------------
    def _ep_discovery(self, h, method, q, f):
        self._send(h, 200, self.discovery())

    def _ep_certs(self, h, method, q, f):
        self._send(h, 200, self.jwks())

    def _redirect_ok(self, uri):
        """KC: a registered `http://127.0.0.1/*` also matches any port (RFC 8252 loopback)."""
        pat = urllib.parse.urlparse(self.redirect_pattern.rstrip("*"))
        got = urllib.parse.urlparse(uri or "")
        if not uri or got.scheme != pat.scheme or got.hostname != pat.hostname or got.fragment:
            return False
        if pat.hostname not in ("127.0.0.1", "localhost", "[::1]") and got.port != pat.port:
            return False
        return got.path.startswith(pat.path) if self.redirect_pattern.endswith("*") else got.path == pat.path

    def _ep_auth(self, h, method, q, f):
        self.auth_requests.append(q)
        if q.get("client_id") != self.client_id:  # KC: shown to the user, never redirected
            return self._page(h, 400, "We are sorry...", "Client not found.")
        uri = q.get("redirect_uri")
        if not self._redirect_ok(uri):
            return self._page(h, 400, "We are sorry...", "Invalid parameter: redirect_uri")

        def back(error, description):
            params = {"error": error, "error_description": description, "state": q.get("state", "")}
            self._redirect(h, uri + "?" + urllib.parse.urlencode(params))

        if q.get("response_type") != "code":
            return back("unsupported_response_type", "Unsupported response type")  # KC: wording unverified
        if self.pkce_required and q.get("code_challenge_method") != "S256":
            return back("invalid_request", "Missing parameter: code_challenge_method")
        with self.lock:
            for e in list(self._login_errors):
                if e[2] is not None and e[2] <= 0:
                    self._login_errors.remove(e)
            if self._login_errors:
                err = self._login_errors[0]
                if err[2] is not None:
                    err[2] -= 1
                return back(err[0], err[1])
        if self.interactive:
            session_code = self._hex(12)
            cookie = self._hex(16)
            self.auth_sessions[session_code] = {"query": q, "cookie": cookie}
            return self._login_form(h, session_code, cookie)
        user = self._browser_user()
        if user is None or not user.enabled:
            return self._page(h, 200, "Account is disabled", "Account is disabled, contact your administrator.")
        self._complete_login(h, q, user)

    def _browser_user(self):
        if self.next_username:
            return self.users[self.next_username]
        n = self.next_user
        return self._adhoc_user(n["subject"], n["name"], n.get("claims"))

    def _complete_login(self, h, q, user, headers=None):
        with self.lock:
            sess = _Session("sid-" + self._hex() if user.id in self._legacy else self._uuid(), user.id, self._now())
            self.sessions[sess.id] = sess
            code = "code-" + self._hex()
            self.codes[code] = {"user": user.id, "sid": sess.id, "nonce": q.get("nonce"),
                                "challenge": q.get("code_challenge"), "method": q.get("code_challenge_method"),
                                "redirect_uri": q["redirect_uri"], "t": self.now()}
        params = [("state", q.get("state", "")), ("session_state", sess.id), ("iss", self.issuer), ("code", code)]
        self._redirect(h, q["redirect_uri"] + "?" + urllib.parse.urlencode(params), headers)

    def _login_form(self, h, session_code, cookie, error=None):
        action = "%s/login-actions/authenticate?%s" % (self.issuer, urllib.parse.urlencode(
            {"session_code": session_code, "client_id": self.client_id, "tab_id": "t" + session_code[:6]}))
        err = '<span id="input-error" class="kc-feedback-text">%s</span>' % html.escape(error) if error else ""
        page = ('<html><head><title>Sign in to %s</title></head><body><div id="kc-header">%s</div>%s'
                '<form id="kc-form-login" action="%s" method="post">'
                '<input id="username" name="username" type="text" autofocus>'
                '<input id="password" name="password" type="password">'
                '<input name="credentialId" type="hidden"><input id="kc-login" type="submit" value="Sign In">'
                '</form></body></html>') % (self.realm, self.realm, err, html.escape(action))
        self._send(h, 200, page, "text/html; charset=utf-8",
                   {"Set-Cookie": "AUTH_SESSION_ID=%s; Path=/realms/%s/; HttpOnly" % (cookie, self.realm)})

    def _ep_login(self, h, method, q, f):
        entry = self.auth_sessions.get(q.get("session_code", ""))
        if method != "POST" or entry is None:
            return self._page(h, 400, "We are sorry...", "Page has expired. Restart the login process.")
        cookies = http.cookies.SimpleCookie(h.headers.get("Cookie", ""))
        if "AUTH_SESSION_ID" not in cookies or cookies["AUTH_SESSION_ID"].value != entry["cookie"]:
            return self._page(h, 400, "We are sorry...",
                              "Cookie not found. Please make sure cookies are enabled in your browser.")
        user = self.users.get(f.get("username", ""))
        if user is not None and user.enabled and user.password == f.get("password"):
            with self.lock:
                self.auth_sessions.pop(q["session_code"], None)
            return self._complete_login(h, entry["query"], user)
        if user is not None and not user.enabled:  # KC: checked before the password
            return self._login_form(h, q["session_code"], entry["cookie"], "Account is disabled, contact your administrator.")
        self._login_form(h, q["session_code"], entry["cookie"], "Invalid username or password.")

    def _ep_token(self, h, method, q, f):
        if method != "POST":
            return self._error(h, 405, "invalid_request", "HTTP method not allowed")
        self.token_requests.append(f)
        if f.get("client_id") != self.client_id:
            return self._error(h, 401, "unauthorized_client", "Invalid client or Invalid client credentials")
        grant = f.get("grant_type")
        if grant == "authorization_code":
            return self._grant_code(h, f)
        if grant == "refresh_token":
            return self._grant_refresh(h, f)
        if not grant:
            return self._error(h, 400, "invalid_request", "Missing form parameter: grant_type")
        self._error(h, 400, "unsupported_grant_type", "Unsupported grant_type")

    def _grant_code(self, h, f):
        with self.lock:
            entry = self.codes.pop(f.get("code", ""), None)
        if entry is None or self.now() - entry["t"] > 60:
            return self._error(h, 400, "invalid_grant", "Code not valid")
        if entry["redirect_uri"] != f.get("redirect_uri"):
            return self._error(h, 400, "invalid_grant", "Incorrect redirect_uri")
        if entry["challenge"]:
            verifier = f.get("code_verifier")
            if not verifier:
                return self._error(h, 400, "invalid_grant", "PKCE code verifier not specified")
            if b64u(hashlib.sha256(verifier.encode()).digest()) != entry["challenge"]:
                return self._error(h, 400, "invalid_grant", "PKCE verification failed: Invalid code verifier")
        user, sess = self._user_by_id(entry["user"]), self.sessions[entry["sid"]]
        with self.lock:
            rt = self._new_refresh_token(sess)
        self._send(h, 200, self._token_response(user, sess, entry["nonce"], rt))

    def _grant_refresh(self, h, f):
        token = f.get("refresh_token")
        if not token:
            return self._error(h, 400, "invalid_request", "Missing form parameter: refresh_token")
        with self.lock:
            rt = self.refresh.get(token)
            if rt is None:
                return self._error(h, 400, "invalid_grant", "Invalid refresh token")
            sess = self.sessions[rt["sid"]]
            user = self._user_by_id(sess.user_id)
            now = self._now()  # fractional: a 1 s lifespan must not round either way
            if not sess.active:
                return self._error(h, 400, "invalid_grant", "Session not active")
            if now - sess.last_refresh > self.settings["sso_session_idle_timeout"] or \
                    now - sess.created > self.settings["sso_session_max_lifespan"]:
                sess.active = False
                return self._error(h, 400, "invalid_grant", "Token is not active")
            if not user.enabled:
                return self._error(h, 400, "invalid_grant", "User disabled")  # KC: wording unverified
            if rt["used"] and self.settings["revoke_refresh_token"]:
                return self._error(h, 400, "invalid_grant", "Maximum allowed refresh token reuse exceeded")
            rt["used"] = True
            sess.last_refresh = now
            fresh = self._new_refresh_token(sess)
        # Refresh-derived ID tokens carry no nonce (auth.md §5.3 rule 6).
        self._send(h, 200, self._token_response(user, sess, None, fresh))

    def _ep_logout(self, h, method, q, f):
        params = {**q, **f}
        hint = params.get("id_token_hint")
        if hint:
            try:
                sid = json.loads(jc.b64u_decode(hint.split(".")[1])).get("sid")
                if sid in self.sessions:
                    self.sessions[sid].active = False
            except (IndexError, ValueError):
                return self._page(h, 400, "We are sorry...", "Invalid parameter: id_token_hint")
        target = params.get("post_logout_redirect_uri")
        if target and self._redirect_ok(target):
            return self._redirect(h, target)
        self._page(h, 200, "Logging out", "You are logged out.")

    def _bearer(self, h):
        token = h.headers.get("Authorization", "")[7:] if h.headers.get("Authorization", "").startswith("Bearer ") else ""
        try:
            head, body, sig = token.split(".")
            key = self.key(json.loads(jc.b64u_decode(head)).get("kid"))
            if not key or not key.verify((head + "." + body).encode(), jc.b64u_decode(sig)):
                return None
            claims = json.loads(jc.b64u_decode(body))
        except ValueError:
            return None
        sess = self.sessions.get(claims.get("sid"))
        if claims.get("exp", 0) < self.now() or not sess or not sess.active:
            return None
        return claims

    def _ep_userinfo(self, h, method, q, f):
        claims = self._bearer(h)
        if claims is None:
            return self._error(h, 401, "invalid_token", "Token verification failed")
        keep = ("sub", "email_verified", "name", "preferred_username", "email")
        self._send(h, 200, {k: claims[k] for k in keep if k in claims})

    def _ep_introspect(self, h, method, q, f):
        token = f.get("token", "")
        try:
            claims = json.loads(jc.b64u_decode(token.split(".")[1]))
            sess = self.sessions.get(claims.get("sid"))
            active = bool(sess and sess.active and claims.get("exp", 0) >= self.now())
        except (IndexError, ValueError):
            return self._send(h, 200, {"active": False})
        self._send(h, 200, dict(claims, active=True, client_id=self.client_id) if active else {"active": False})

    # -- IdpBackend: the browser -----------------------------------------------------------------
    def browser_login(self, auth_url, username, password):
        jar = http.cookiejar.CookieJar()
        opener = urllib.request.build_opener(urllib.request.HTTPCookieProcessor(jar), _Redirect)

        def go(request):
            try:
                with opener.open(request, timeout=10) as r:
                    return r.status, dict(r.headers), r.read().decode()
            except urllib.error.HTTPError as e:
                return e.code, dict(e.headers), e.read().decode()

        status, headers, page = go(auth_url)
        if status == 302 and "login-actions" not in headers.get("Location", ""):
            return self._login_result(headers["Location"])
        if status == 200 and "kc-form-login" in page:
            action = html.unescape(re.search(r'id="kc-form-login"[^>]*action="([^"]+)"', page).group(1))
            data = urllib.parse.urlencode({"username": username, "password": password, "credentialId": ""}).encode()
            status, headers, page = go(urllib.request.Request(action, data=data, method="POST"))
            if status == 302:
                return self._login_result(headers["Location"])
            m = re.search(r'id="input-error"[^>]*>([^<]*)<', page)
            raise LoginRefused(html.unescape(m.group(1)) if m else "sign-in failed (HTTP %d)" % status)
        m = re.search(r'class="instruction">([^<]*)<', page)
        raise LoginRefused(html.unescape(m.group(1)) if m else "sign-in failed (HTTP %d)" % status)

    def _login_result(self, location):
        q = dict(urllib.parse.parse_qsl(urllib.parse.urlparse(location).query))
        if "error" in q:
            raise LoginRefused("%s: %s" % (q["error"], q.get("error_description", "")))
        return location

    def obtain_tokens(self, username, password):
        if not self.interactive:  # /auth signs in whoever is "at the browser": make that this user
            user = self.users.get(username)
            if user is None or user.password != password or not user.enabled:
                raise LoginRefused("Invalid username or password.")
            previous, self.next_username = self.next_username, username
            try:
                return self._obtain(username, password)
            finally:
                self.next_username = previous
        return self._obtain(username, password)

    def _obtain(self, username, password):
        verifier = b64u(secrets.token_bytes(32))
        redirect = "http://127.0.0.1:9/obtain"  # never contacted: we read the Location, not follow it
        auth_url = self._oidc + "/auth?" + urllib.parse.urlencode({
            "client_id": self.client_id, "response_type": "code", "scope": "openid", "redirect_uri": redirect,
            "state": "s", "nonce": b64u(secrets.token_bytes(8)),
            "code_challenge": b64u(hashlib.sha256(verifier.encode()).digest()), "code_challenge_method": "S256"})
        location = self.browser_login(auth_url, username, password)
        code = dict(urllib.parse.parse_qsl(urllib.parse.urlparse(location).query))["code"]
        data = urllib.parse.urlencode({"grant_type": "authorization_code", "client_id": self.client_id,
                                       "code": code, "redirect_uri": redirect, "code_verifier": verifier}).encode()
        with urllib.request.urlopen(urllib.request.Request(self._oidc + "/token", data=data), timeout=10) as r:
            return json.loads(r.read())


def main(argv=None):
    import argparse
    ap = argparse.ArgumentParser(description="Run the emulator by hand (it prints the issuer) or dump fixtures.")
    ap.add_argument("--alg", default="RS256", choices=("RS256", "ES256"))
    ap.add_argument("--dump-fixtures", metavar="DIR", help="write the golden fixtures for tests/unit (K3.1) and exit")
    args = ap.parse_args(argv)
    if args.dump_fixtures:
        from . import fixture_dump
        return fixture_dump.dump(pathlib.Path(args.dump_fixtures))
    idp = MockKeycloak(alg=args.alg).start()
    idp.add_user("alice", "pw", groups=["admins"])
    print("issuer: %s  client_id: %s  (user alice/pw; Ctrl-C to stop)" % (idp.issuer, idp.client_id))
    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        idp.stop()


if __name__ == "__main__":
    main()
