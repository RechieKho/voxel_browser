"""A tiny OpenID Connect provider for the auth e2e tests (stdlib only).

Serves discovery, a JWKS, an authorization endpoint that "logs the user in" by redirecting
straight back to the client's loopback redirect_uri, and a token endpoint (authorization_code
with PKCE, and refresh_token). Tokens are RS256, signed with the TEST-ONLY key in
tests/e2e/fixtures/idp_rsa.json by pure-Python PKCS#1 v1.5 (no third-party crypto, so the
harness still needs nothing but pytest).

    idp = MockIdp(); idp.start()
    token = idp.mint("sub-alice", "alice")          # what a real IdP would have issued
    idp.issuer, idp.client_id                        # put these in the pack's auth.lua
"""
import base64
import hashlib
import json
import pathlib
import secrets
import threading
import time
import urllib.parse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

FIXTURE = pathlib.Path(__file__).resolve().parents[1] / "fixtures" / "idp_rsa.json"
# DER prefix of DigestInfo for SHA-256 (RFC 8017 §9.2 note 1).
SHA256_PREFIX = bytes.fromhex("3031300d060960864801650304020105000420")


def b64u(data):
    return base64.urlsafe_b64encode(data).rstrip(b"=").decode()


def b64u_int(i):
    return b64u(i.to_bytes((i.bit_length() + 7) // 8, "big"))


def _load_key():
    raw = json.loads(FIXTURE.read_text())
    return raw["kid"], int(raw["n"], 16), int(raw["e"], 16), int(raw["d"], 16)


class MockIdp:
    def __init__(self, client_id="vb-e2e", realm="e2e"):
        self.kid, self.n, self.e, self.d = _load_key()
        self.client_id = client_id
        self.realm = realm
        self.logins = {}          # who the "user at the browser" is next: {"subject":..., "name":...}
        self.refresh_tokens = {}  # refresh token -> {"subject", "name", "claims"}
        self.codes = {}           # auth code -> {"user", "nonce", "challenge", "redirect_uri"}
        self.token_requests = []  # form dicts the token endpoint received (for assertions)
        self.auth_requests = []   # query dicts the authorization endpoint received
        self.next_user = {"subject": "sub-browser", "name": "browser-user", "claims": {}}
        self.lock = threading.Lock()
        self._server = None
        self.port = 0

    # -- lifecycle ---------------------------------------------------------
    def start(self):
        idp = self

        class Handler(BaseHTTPRequestHandler):
            def log_message(self, *a):  # keep pytest output clean
                pass

            def _send(self, status, body, ctype="application/json", headers=None):
                data = body if isinstance(body, bytes) else json.dumps(body).encode()
                self.send_response(status)
                self.send_header("Content-Type", ctype)
                self.send_header("Content-Length", str(len(data)))
                for k, v in (headers or {}).items():
                    self.send_header(k, v)
                self.end_headers()
                self.wfile.write(data)

            def do_GET(self):
                url = urllib.parse.urlparse(self.path)
                base = "/realms/" + idp.realm
                if url.path == base + "/.well-known/openid-configuration":
                    return self._send(200, {"issuer": idp.issuer,
                                            "authorization_endpoint": idp.issuer + "/auth",
                                            "token_endpoint": idp.issuer + "/token",
                                            "jwks_uri": idp.issuer + "/certs"})
                if url.path == base + "/certs":
                    return self._send(200, idp.jwks())
                if url.path == base + "/auth":
                    q = {k: v[0] for k, v in urllib.parse.parse_qs(url.query).items()}
                    return idp._authorize(self, q)
                self._send(404, {"error": "not_found"})

            def do_POST(self):
                length = int(self.headers.get("Content-Length", "0"))
                form = {k: v[0] for k, v in urllib.parse.parse_qs(self.rfile.read(length).decode()).items()}
                if self.path == "/realms/%s/token" % idp.realm:
                    return idp._token(self, form)
                self._send(404, {"error": "not_found"})

        self._server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        self.port = self._server.server_address[1]
        threading.Thread(target=self._server.serve_forever, daemon=True).start()
        return self

    def stop(self):
        if self._server:
            self._server.shutdown()
            self._server.server_close()

    @property
    def issuer(self):
        return "http://127.0.0.1:%d/realms/%s" % (self.port, self.realm)

    def jwks(self):
        return {"keys": [{"kty": "RSA", "kid": self.kid, "use": "sig", "alg": "RS256",
                          "n": b64u_int(self.n), "e": b64u_int(self.e)}]}

    # -- tokens --------------------------------------------------------------
    def _sign(self, signing_input):
        digest = hashlib.sha256(signing_input).digest()
        k = (self.n.bit_length() + 7) // 8
        t = SHA256_PREFIX + digest
        em = b"\x00\x01" + b"\xff" * (k - len(t) - 3) + b"\x00" + t
        return pow(int.from_bytes(em, "big"), self.d, self.n).to_bytes(k, "big")

    def mint(self, subject, name, claims=None, nonce=None, iat=None, exp=None, aud=None, iss=None,
             alg="RS256", kid=None, tamper=False):
        """An ID token as this IdP would issue it. Override fields to make a *bad* one."""
        now = int(time.time())
        iat = now if iat is None else iat
        payload = {"iss": iss or self.issuer, "aud": aud or self.client_id, "sub": subject,
                   "iat": iat, "exp": iat + 600 if exp is None else exp, "preferred_username": name}
        payload.update(claims or {})
        if nonce:
            payload["nonce"] = nonce
        header = {"alg": alg, "kid": kid or self.kid, "typ": "JWT"}
        signing_input = (b64u(json.dumps(header).encode()) + "." + b64u(json.dumps(payload).encode())).encode()
        sig = self._sign(signing_input)
        token = signing_input.decode() + "." + b64u(sig)
        if tamper:  # change the payload after signing
            head, _, tail = token.partition(".")
            body, _, s = tail.partition(".")
            forged = dict(payload, sub="admin")
            token = head + "." + b64u(json.dumps(forged).encode()) + "." + s
        return token

    def issue_refresh_token(self, subject, name, claims=None):
        rt = "rt-" + secrets.token_hex(8)
        self.refresh_tokens[rt] = {"subject": subject, "name": name, "claims": claims or {}}
        return rt

    def revoke_all(self, subject=None):
        """An admin ends the user's sessions: refresh tokens stop working (instant at the IdP)."""
        with self.lock:
            for rt in [r for r, u in self.refresh_tokens.items() if subject in (None, u["subject"])]:
                del self.refresh_tokens[rt]

    def set_claims(self, subject, claims):
        """Change what the next token for `subject` carries (e.g. a group was removed)."""
        with self.lock:
            for u in self.refresh_tokens.values():
                if u["subject"] == subject:
                    u["claims"] = claims

    # -- endpoints -------------------------------------------------------------
    def _authorize(self, handler, q):
        self.auth_requests.append(q)
        if q.get("response_type") != "code" or q.get("code_challenge_method") != "S256" \
                or q.get("client_id") != self.client_id or "redirect_uri" not in q:
            return handler._send(400, {"error": "invalid_request"})
        code = "code-" + secrets.token_hex(8)
        with self.lock:
            self.codes[code] = {"user": dict(self.next_user), "nonce": q.get("nonce"),
                                "challenge": q.get("code_challenge"), "redirect_uri": q["redirect_uri"]}
        loc = q["redirect_uri"] + "?" + urllib.parse.urlencode({"code": code, "state": q.get("state", "")})
        handler._send(302, b"", "text/plain", {"Location": loc})

    def _token(self, handler, form):
        self.token_requests.append(form)
        grant = form.get("grant_type")
        if grant == "authorization_code":
            with self.lock:
                entry = self.codes.pop(form.get("code", ""), None)
            if entry is None or entry["redirect_uri"] != form.get("redirect_uri"):
                return handler._send(400, {"error": "invalid_grant"})
            verifier = form.get("code_verifier", "")
            if b64u(hashlib.sha256(verifier.encode()).digest()) != entry["challenge"]:
                return handler._send(400, {"error": "invalid_grant"})  # PKCE failed
            u = entry["user"]
            rt = self.issue_refresh_token(u["subject"], u["name"], u.get("claims"))
            return handler._send(200, {"id_token": self.mint(u["subject"], u["name"], u.get("claims"),
                                                             nonce=entry["nonce"]),
                                       "refresh_token": rt, "token_type": "Bearer"})
        if grant == "refresh_token":
            with self.lock:
                u = self.refresh_tokens.get(form.get("refresh_token", ""))
            if u is None:
                return handler._send(400, {"error": "invalid_grant"})
            # Refresh-derived ID tokens carry no nonce (auth.md §5.3 rule 6).
            return handler._send(200, {"id_token": self.mint(u["subject"], u["name"], u["claims"]),
                                       "refresh_token": form["refresh_token"]})
        handler._send(400, {"error": "unsupported_grant_type"})


def auth_lua(idp, name_claim="preferred_username", claims=("email", "groups"), reauth_interval=0,
             reauth_grace=120, extra=""):
    """The pack's auth.lua pointing at `idp` (what an operator would write)."""
    lines = ["return {", '\tprovider = "keycloak",', '\tdisplay_name = "E2E realm",',
             '\tissuer = "%s",' % idp.issuer, '\tclient_id = "%s",' % idp.client_id,
             '\tname_claim = "%s",' % name_claim,
             "\tclaims = { %s }," % ", ".join('"%s"' % c for c in claims)]
    if reauth_interval:
        lines.append("\treauth_interval_seconds = %d," % reauth_interval)
        lines.append("\treauth_grace_seconds = %d," % reauth_grace)
    lines.append(extra)
    lines.append("}")
    return "\n".join(lines) + "\n"
