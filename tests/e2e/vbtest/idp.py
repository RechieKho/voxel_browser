"""What the auth tests may ask of an identity provider, whichever one is behind it.

`IdpBackend` is implemented by `MockKeycloak` (vbtest/mock_keycloak.py, the default) and by
`RealKeycloak` (vbtest/keycloak_real.py, `--vb-idp=keycloak`). Tests use only this surface, so
the same test file runs against both; a test that needs `mint()` or fault injection is marked
`mock_only` and is skipped against a real Keycloak.

Also here: `auth_lua()` (the pack's auth.lua an operator would write) and `BrowserPlayer`
(plays the user's browser while a real client waits for the redirect).
"""
import hashlib
import html
import http.cookiejar
import json
import re
import secrets
import threading
import time
import urllib.error
import urllib.parse
import urllib.request

from . import jwscrypto as jc


class NotSupported(Exception):
    """The backend cannot do this (e.g. mint() on a real Keycloak). Mark the test `mock_only`."""


class LoginRefused(Exception):
    """The IdP did not complete a browser sign-in; `str(e)` is the message it showed the user.
    `redirect` is set when the IdP sent the browser back to the client with `error=...` (a real
    browser would follow it, so the client learns the sign-in was cancelled)."""

    def __init__(self, message, redirect=None):
        super().__init__(message)
        self.redirect = redirect


class _NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, *a, **kw):  # never follow: the caller wants to see the Location
        return None


class IdpBackend:
    """Interface only; see the module docstring. Attributes: issuer, client_id, realm."""

    issuer = ""
    client_id = ""
    realm = ""

    # -- users and admin operations (identical names on every backend) ---------------------
    def add_user(self, username, password, *, email=None, groups=(), roles=(), enabled=True,
                 email_verified=True, name=None):
        raise NotImplementedError

    def user_id(self, username):
        """The user's `sub` claim."""
        raise NotImplementedError

    def disable_user(self, username):
        raise NotImplementedError

    def enable_user(self, username):
        raise NotImplementedError

    def set_groups(self, username, groups):
        raise NotImplementedError

    def set_roles(self, username, roles):
        raise NotImplementedError

    def admin_logout(self, username):
        """End every SSO session of the user: their refresh tokens stop working."""
        raise NotImplementedError

    def set_realm(self, **settings):
        """sso_session_idle_timeout, sso_session_max_lifespan, access_token_lifespan,
        revoke_refresh_token (seconds / bool)."""
        raise NotImplementedError

    def rotate_keys(self):
        """A new signing key becomes active; the old one stays in the JWKS."""
        raise NotImplementedError

    def retire_key(self, kid):
        """Remove a (passive) key from the JWKS."""
        raise NotImplementedError

    # -- the browser and the tokens it yields -------------------------------------------------
    # Where clients reach the realm (`<scheme>://127.0.0.1:<port>/realms/<realm>`); backends set it.
    local_url = ""

    def browser_login(self, auth_url, username, password):
        """Play the user's browser against `auth_url` (no redirects followed past the client's).
        Returns the final redirect URL (the client's loopback `redirect_uri` with `code`),
        or raises LoginRefused with the message the IdP showed. Shared by every backend: it
        needs only HTTP, a cookie jar and Keycloak's login form (`id="kc-form-login"`)."""
        # Keycloak marks its cookies `Secure; SameSite=None` even on http://127.0.0.1, which real
        # browsers accept for loopback; Python's jar must be told to send them over http too.
        jar = http.cookiejar.CookieJar(http.cookiejar.DefaultCookiePolicy(secure_protocols=("https", "wss", "http")))
        opener = urllib.request.build_opener(urllib.request.HTTPCookieProcessor(jar), _NoRedirect)

        def go(request):
            try:
                with opener.open(request, timeout=20) as r:
                    return r.status, dict(r.headers), r.read().decode("utf-8", "replace")
            except urllib.error.HTTPError as e:
                return e.code, dict(e.headers), e.read().decode("utf-8", "replace")

        status, headers, page = go(auth_url)
        if status == 302:
            return self._login_result(headers["Location"])
        form = re.search(r'<form[^>]*id="kc-form-login"[^>]*>', page) if status == 200 else None
        if form:
            action = html.unescape(re.search(r'action="([^"]+)"', form.group(0)).group(1))
            data = urllib.parse.urlencode({"username": username, "password": password, "credentialId": ""}).encode()
            status, headers, page = go(urllib.request.Request(action, data=data, method="POST"))
            if status == 302:
                return self._login_result(headers["Location"])
            m = re.search(r'class="[^"]*kc-feedback-text[^"]*"[^>]*>\s*([^<]+?)\s*<', page)
            raise LoginRefused(html.unescape(m.group(1)) if m else "sign-in failed (HTTP %d)" % status)
        m = re.search(r'class="instruction">([^<]*)<', page) or re.search(r'id="kc-error-message"[^>]*>.*?<p[^>]*>([^<]*)<', page, re.S)
        raise LoginRefused(html.unescape(m.group(1)).strip() if m else "sign-in failed (HTTP %d)" % status)

    @staticmethod
    def _login_result(location):
        q = dict(urllib.parse.parse_qsl(urllib.parse.urlparse(location).query))
        if "error" in q:
            raise LoginRefused("%s: %s" % (q["error"], q.get("error_description", "")), redirect=location)
        return location

    def obtain_tokens(self, username, password):
        """Sign `username` in through the real code flow with PKCE, as a throw-away client
        would, and return the token response ({"id_token", "refresh_token", ...}). This is
        how tests get a token-file token that works on every backend. The authorization request
        carries no `nonce` (the server's per-connection nonce is unknowable here), so the ID token
        has none either and is bound by the server's freshness rule instead (auth.md 5.3 rule 6)."""
        verifier = jc.b64u(secrets.token_bytes(32))
        redirect = "http://127.0.0.1:9/obtain"  # never contacted: we read the Location, not follow it
        oidc = self.local_url + "/protocol/openid-connect"
        auth_url = oidc + "/auth?" + urllib.parse.urlencode({
            "client_id": self.client_id, "response_type": "code", "scope": "openid", "redirect_uri": redirect,
            "state": "s", "code_challenge": jc.b64u(hashlib.sha256(verifier.encode()).digest()),
            "code_challenge_method": "S256"})
        location = self.browser_login(auth_url, username, password)
        code = dict(urllib.parse.parse_qsl(urllib.parse.urlparse(location).query))["code"]
        data = urllib.parse.urlencode({"grant_type": "authorization_code", "client_id": self.client_id,
                                       "code": code, "redirect_uri": redirect, "code_verifier": verifier}).encode()
        with urllib.request.urlopen(urllib.request.Request(oidc + "/token", data=data), timeout=20) as r:
            return json.loads(r.read())

    def mint(self, *args, **kwargs):
        raise NotSupported("mint() needs MockKeycloak")

    def request_log(self):
        """Requests the IdP served, oldest first (dicts; secrets already shortened)."""
        return []

    def stop(self):
        pass


def eventually(condition, description, timeout=10.0, interval=0.05):
    """Poll `condition()` (for state that is not on an automation channel, e.g. the IdP's request
    log) until it is truthy; AssertionError naming `description` on timeout. Scaled like every wait."""
    from .process import timeout_scale
    deadline = time.time() + timeout * timeout_scale()
    while True:
        result = condition()
        if result:
            return result
        if time.time() > deadline:
            raise AssertionError("expected %s within %.1fs" % (description, timeout * timeout_scale()))
        time.sleep(interval)


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


class BrowserPlayer:
    """A thread that plays the user's browser for one sign-in.

    The engine writes the authorization URL to VB_AUTH_URL_FILE instead of opening a browser
    (set it with monkeypatch.setenv before the client starts). This waits for the file, signs
    in at the IdP with `idp.browser_login` (or with `login=callable(url)` for a custom
    script, e.g. one that cancels), then delivers the final redirect to the client's loopback
    listener like a real browser.

        player = BrowserPlayer(idp, url_file, "alice", "pw").start()
        (alice,) = factory(1)
        player.join()          # re-raises anything that went wrong; player.page is the HTML shown
    """

    def __init__(self, idp, url_file, username=None, password=None, login=None, deliver=True,
                 timeout=30):
        self.idp, self.url_file, self.timeout, self.deliver = idp, url_file, timeout, deliver
        self.username, self.password, self.login = username, password, login
        self.auth_url = self.redirect = self.page = self.status = self.error = self.refused = None
        self._thread = threading.Thread(target=self._run, daemon=True)

    def start(self):
        self._thread.start()
        return self

    def _run(self):
        try:
            deadline = time.time() + self.timeout
            while time.time() < deadline:
                if self.url_file.exists() and self.url_file.read_text():
                    break
                time.sleep(0.05)
            else:
                raise TimeoutError("the client never wrote an authorization URL to %s" % self.url_file)
            self.auth_url = self.url_file.read_text()
            if self.login:
                self.redirect = self.login(self.auth_url)
            else:
                self.redirect = self.idp.browser_login(self.auth_url, self.username, self.password)
            if self.redirect and self.deliver:
                self._deliver(self.redirect)
        except LoginRefused as e:  # the IdP said no: data for the test, not a crash
            self.refused = e
            try:
                if e.redirect and self.deliver:  # a real browser follows an error redirect back to the client
                    self._deliver(e.redirect)
            except Exception as err:
                self.error = err
        except Exception as e:  # reported by join(), in the test's own thread
            self.error = e

    def _deliver(self, url):
        """GET the client's loopback redirect like a browser, keeping the page whatever the status."""
        try:
            with urllib.request.urlopen(url, timeout=10) as r:
                self.status, self.page = r.status, r.read().decode()
        except urllib.error.HTTPError as e:
            self.status, self.page = e.code, e.read().decode()

    def join(self, timeout=15, raises=True):
        self._thread.join(timeout)
        if raises and self.error is not None:
            raise self.error
        return self
