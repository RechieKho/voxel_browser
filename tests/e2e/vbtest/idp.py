"""What the auth tests may ask of an identity provider, whichever one is behind it.

`IdpBackend` is implemented by `MockKeycloak` (vbtest/mock_keycloak.py, the default) and by
`RealKeycloak` (vbtest/keycloak_real.py, `--vb-idp=keycloak`). Tests use only this surface, so
the same test file runs against both; a test that needs `mint()` or fault injection is marked
`mock_only` and is skipped against a real Keycloak.

Also here: `auth_lua()` (the pack's auth.lua an operator would write) and `BrowserPlayer`
(plays the user's browser while a real client waits for the redirect).
"""
import threading
import time
import urllib.request


class NotSupported(Exception):
    """The backend cannot do this (e.g. mint() on a real Keycloak). Mark the test `mock_only`."""


class LoginRefused(Exception):
    """The IdP did not complete a browser sign-in; `str(e)` is the message it showed the user."""


class IdpBackend:
    """Interface only; see the module docstring. Attributes: issuer, client_id, realm."""

    issuer = ""
    client_id = ""
    realm = ""

    # -- users and admin operations (identical names on every backend) ---------------------
    def add_user(self, username, password, *, email=None, groups=(), roles=(), enabled=True,
                 email_verified=True, name=None):
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
    def browser_login(self, auth_url, username, password):
        """Play the user's browser against `auth_url` (no redirects followed past the client's).
        Returns the final redirect URL (the client's loopback `redirect_uri` with `code`),
        or raises LoginRefused with the message the IdP showed."""
        raise NotImplementedError

    def obtain_tokens(self, username, password):
        """Sign `username` in through the real code flow with PKCE, as a throw-away client
        would, and return the token response ({"id_token", "refresh_token", ...}). This is
        how tests get a token-file token that works on every backend."""
        raise NotImplementedError

    def mint(self, *args, **kwargs):
        raise NotSupported("mint() needs MockKeycloak")

    def request_log(self):
        """Requests the IdP served, oldest first (dicts; secrets already shortened)."""
        return []

    def stop(self):
        pass


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
        self.auth_url = self.redirect = self.page = self.status = self.error = None
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
                with urllib.request.urlopen(self.redirect, timeout=10) as r:
                    self.status, self.page = r.status, r.read().decode()
        except Exception as e:  # reported by join(), in the test's own thread
            self.error = e

    def join(self, timeout=15, raises=True):
        self._thread.join(timeout)
        if raises and self.error is not None:
            raise self.error
        return self
