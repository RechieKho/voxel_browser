"""Self-tests for the Keycloak emulator (vbtest/mock_keycloak.py). No game binaries.

Emulator bugs should look like emulator bugs, not like engine bugs, so this runs in seconds and
needs nothing but pytest. Run it alone: `python3 -m pytest tests/e2e/test_mock_keycloak.py -q`.
"""
import hashlib
import html
import json
import re
import threading
import time
import urllib.error
import urllib.parse
import urllib.request

import pytest

from vbtest import jwscrypto as jc
from vbtest.idp import LoginRefused, NotSupported
from vbtest.mock_keycloak import MockKeycloak

REDIRECT = "http://127.0.0.1:5555/cb"


# -- helpers ---------------------------------------------------------------------------------------
class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, *a, **kw):
        return None


def http(url, data=None, headers=None, method=None):
    """(status, headers, body bytes); never raises on an HTTP error status and never follows redirects."""
    opener = urllib.request.build_opener(NoRedirect)
    req = urllib.request.Request(url, data=urllib.parse.urlencode(data).encode() if data is not None else None,
                                 headers=headers or {}, method=method)
    try:
        with opener.open(req, timeout=10) as r:
            return r.status, dict(r.headers), r.read()
    except urllib.error.HTTPError as e:
        return e.code, dict(e.headers), e.read()


def jget(url):
    status, _, body = http(url)
    return status, json.loads(body)


def pkce():
    verifier = jc.b64u(b"v" * 32)
    return verifier, jc.b64u(hashlib.sha256(verifier.encode()).digest())


def auth_url(idp, challenge, **over):
    q = {"client_id": idp.client_id, "response_type": "code", "scope": "openid", "redirect_uri": REDIRECT,
         "state": "st", "nonce": "n-1", "code_challenge": challenge, "code_challenge_method": "S256"}
    q.update(over)
    return idp.issuer + "/protocol/openid-connect/auth?" + urllib.parse.urlencode({k: v for k, v in q.items() if v is not None})


def token(idp, **form):
    form.setdefault("client_id", idp.client_id)
    status, _, body = http(idp.issuer + "/protocol/openid-connect/token", data=form)
    return status, json.loads(body)


def claims(jwt):
    return json.loads(jc.b64u_decode(jwt.split(".")[1]))


def code_from(location):
    return dict(urllib.parse.parse_qsl(urllib.parse.urlparse(location).query))


def sign_in(idp, user="alice", **auth_over):
    """The non-interactive flow by hand: returns (code, verifier, query of the redirect)."""
    idp.login_as(user)
    verifier, challenge = pkce()
    status, headers, _ = http(auth_url(idp, challenge, **auth_over))
    assert status == 302, status
    q = code_from(headers["Location"])
    return q["code"], verifier, q


@pytest.fixture
def realm(idp):
    idp.add_user("alice", "pw", groups=["admins"], roles=["moderator"])
    idp.add_user("bob", "pw")
    return idp


def exchange(idp, code, verifier, **over):
    form = {"grant_type": "authorization_code", "code": code, "redirect_uri": REDIRECT, "code_verifier": verifier}
    form.update(over)
    return token(idp, **form)


# -- K2.1 crypto -----------------------------------------------------------------------------------
class TestCrypto:
    # RFC 6979 §A.2.5 (P-256, SHA-256)
    X = 0xC9AFA9D845BA75166B5C215767B1D6934E50C3DB36E89B127B8A622B120F6721
    UX = 0x60FED4BA255A9D31C961EB74C6356D68C049B8923B61FA6CE669622E60F29FB6
    UY = 0x7903FE1008B8BC99A41AE9E95628BC64F2F1B20C2D7E9F5177A3C294D4462299

    @pytest.mark.parametrize("msg,k,r,s", [
        (b"sample", 0xA6E3C57DD01ABE90086538398355DD4C3B17AA873382B0F24D6129493D8AAD60,
         0xEFD48B2AACB6A8FD1140DD9CD45E81D69D2C877B56AAF991C34D0EA84EAF3716,
         0xF7CB1C942D657C41D436C7A1B6E29F65F3E900DBB9AFF4064DC4AB2F843ACDA8),
        (b"test", 0xD16B6AE827F17175E040871A1C7EC3500192C4C92677336EC2537ACAEE0008E0,
         0xF1ABB023518351CD71D881567B1EA663ED3EFCF6C5132B354F28D3B0B7D38367,
         0x019F4113742A2B14BD25926B49C649155F267E60D3814B4C0CC84250E46F0083)])
    def test_rfc6979_vector(self, msg, k, r, s):
        digest = hashlib.sha256(msg).digest()
        assert jc.public_point(self.X) == (self.UX, self.UY)
        assert jc.rfc6979_k(self.X, digest) == k
        assert jc.ecdsa_sign(digest, self.X) == (r, s)
        assert jc.ecdsa_verify(digest, r, s, (self.UX, self.UY))
        assert not jc.ecdsa_verify(digest, r, s ^ 1, (self.UX, self.UY))

    @pytest.mark.parametrize("alg", ["RS256", "ES256"])
    def test_tokens_verify_against_the_published_jwks(self, alg):
        idp = MockKeycloak(alg=alg).start()
        try:
            jwt = idp.mint("sub-1", "x")
            head, body, sig = jwt.split(".")
            header = json.loads(jc.b64u_decode(head))
            assert header["alg"] == alg
            jwk = next(k for k in idp.jwks()["keys"] if k["kid"] == header["kid"])
            signing_input, signature = (head + "." + body).encode(), jc.b64u_decode(sig)
            if alg == "ES256":
                assert len(signature) == 64  # raw r||s, not DER
                point = (int.from_bytes(jc.b64u_decode(jwk["x"]), "big"), int.from_bytes(jc.b64u_decode(jwk["y"]), "big"))
                assert jc.es256_verify(signing_input, signature, point)
                assert not jc.es256_verify(signing_input + b"x", signature, point)
            else:
                n, e = (int.from_bytes(jc.b64u_decode(jwk[f]), "big") for f in ("n", "e"))
                assert jc.rs256_verify(signing_input, signature, n, e)
                assert not jc.rs256_verify(signing_input + b"x", signature, n, e)
        finally:
            idp.stop()

    def test_tampered_token_fails_verification(self, idp):
        head, body, sig = idp.mint("sub-1", "x", tamper=True).split(".")
        assert claims(head + "." + body + "." + sig)["sub"] == "admin"
        key = idp.key()
        assert not key.verify((head + "." + body).encode(), jc.b64u_decode(sig))


# -- K2.2 protocol ---------------------------------------------------------------------------------
class TestDiscovery:
    def test_fields_and_layout_match_keycloak(self, idp):
        status, doc = jget(idp.issuer + "/.well-known/openid-configuration")
        base = idp.issuer + "/protocol/openid-connect/"
        assert status == 200 and doc["issuer"] == idp.issuer
        assert doc["authorization_endpoint"] == base + "auth" and doc["token_endpoint"] == base + "token"
        assert doc["jwks_uri"] == base + "certs" and doc["end_session_endpoint"] == base + "logout"
        assert doc["introspection_endpoint"] == base + "token/introspect"
        assert doc["userinfo_endpoint"] == base + "userinfo"
        assert doc["code_challenge_methods_supported"] == ["plain", "S256"]
        assert doc["id_token_signing_alg_values_supported"] == ["RS256"]
        assert {"authorization_code", "refresh_token"} <= set(doc["grant_types_supported"])
        assert "code" in doc["response_types_supported"]

    @pytest.mark.realm(alg="ES256")
    def test_es256_realm_says_so(self, idp):
        assert jget(idp.issuer + "/.well-known/openid-configuration")[1]["id_token_signing_alg_values_supported"] == ["ES256"]
        assert {k["kty"] for k in idp.jwks()["keys"] if k["use"] == "sig"} == {"EC"}

    def test_jwks_has_a_signing_key_and_an_encryption_key_the_engine_must_skip(self, idp):
        keys = jget(idp.issuer + "/protocol/openid-connect/certs")[1]["keys"]
        assert [k["use"] for k in keys] == ["sig", "enc"]
        assert keys[1]["alg"] == "RSA-OAEP"


class TestCodeFlow:
    def test_code_flow_with_pkce_succeeds_once(self, realm):
        code, verifier, redirect = sign_in(realm)
        assert redirect["state"] == "st" and redirect["iss"] == realm.issuer and redirect["session_state"]
        status, body = exchange(realm, code, verifier)
        assert status == 200
        assert {"id_token", "access_token", "refresh_token", "token_type", "expires_in"} <= set(body)
        idt = claims(body["id_token"])
        assert idt["nonce"] == "n-1" and idt["typ"] == "ID" and idt["azp"] == "vb-e2e" and idt["aud"] == "vb-e2e"
        assert idt["sid"] == redirect["session_state"] and idt["preferred_username"] == "alice"
        assert idt["groups"] == ["admins"] and "moderator" in idt["realm_access"]["roles"]
        assert idt["at_hash"] == jc.b64u(hashlib.sha256(body["access_token"].encode()).digest()[:16])
        assert claims(body["access_token"])["typ"] == "Bearer" and claims(body["access_token"])["aud"] == "account"
        # a reused code fails exactly like Keycloak
        assert exchange(realm, code, verifier) == (400, {"error": "invalid_grant", "error_description": "Code not valid"})

    def test_wrong_verifier_fails_and_burns_the_code(self, realm):
        code, verifier, _ = sign_in(realm)
        status, body = exchange(realm, code, "w" * 43)
        assert (status, body["error_description"]) == (400, "PKCE verification failed: Code mismatch")
        assert exchange(realm, code, verifier)[1]["error_description"] == "Code not valid"

    def test_missing_verifier_and_wrong_redirect_uri(self, realm):
        code, verifier, _ = sign_in(realm)
        assert exchange(realm, code, None, code_verifier="x")[1]["error_description"] == \
            "PKCE verification failed: Invalid code verifier"   # not 43..128 unreserved characters
        code, verifier, _ = sign_in(realm)
        form = {"grant_type": "authorization_code", "code": code, "redirect_uri": REDIRECT}
        assert token(realm, **form)[1]["error_description"] == "PKCE code verifier not specified"  # absent
        code, verifier, _ = sign_in(realm)
        code, verifier, _ = sign_in(realm)
        assert exchange(realm, code, verifier, redirect_uri="http://127.0.0.1:1/other")[1]["error_description"] == \
            "Incorrect redirect_uri"

    def test_wrong_client_is_unauthorized(self, realm):
        code, verifier, _ = sign_in(realm)
        status, body = exchange(realm, code, verifier, client_id="someone-else")
        assert status == 401 and body["error"] == "invalid_client"

    def test_unknown_and_missing_grants(self, realm):
        assert token(realm, grant_type="password")[1]["error"] == "unsupported_grant_type"
        assert token(realm)[1]["error"] == "invalid_request"
        assert token(realm, grant_type="refresh_token")[1] == {"error": "invalid_request", "error_description": "No refresh token"}

    def test_redirect_uri_not_matching_the_clients_pattern_is_a_400_page_not_a_redirect(self, realm):
        _, challenge = pkce()
        status, headers, page = http(auth_url(realm, challenge, redirect_uri="https://evil.example/cb"))
        assert status == 400 and "Location" not in headers and b"Invalid parameter: redirect_uri" in page
        # any loopback port matches `http://127.0.0.1/*` (RFC 8252)
        assert http(auth_url(realm, challenge, redirect_uri="http://127.0.0.1:1/x/y"))[0] == 302

    def test_unknown_client_is_a_400_page(self, realm):
        _, challenge = pkce()
        status, _, page = http(auth_url(realm, challenge, client_id="nope"))
        assert status == 400 and b"Client not found" in page

    def test_missing_pkce_is_rejected_by_redirect(self, realm):
        status, headers, _ = http(auth_url(realm, None, code_challenge_method=None))
        assert status == 302 and code_from(headers["Location"])["error"] == "invalid_request"
        q = code_from(http(auth_url(realm, "x", code_challenge_method="plain"))[1]["Location"])
        assert "not matching the configured one" in q["error_description"] and q["iss"] == realm.issuer
        q = code_from(http(auth_url(realm, "x", response_type="bogus"))[1]["Location"])
        assert q["error"] == "unsupported_response_type" and "error_description" not in q

    def test_user_cancels(self, realm):
        realm.cancel_logins("access_denied", "User cancelled")
        _, challenge = pkce()
        status, headers, _ = http(auth_url(realm, challenge))
        q = code_from(headers["Location"])
        assert status == 302 and q["error"] == "access_denied" and q["state"] == "st" and "code" not in q
        assert http(auth_url(realm, challenge))[0] == 302  # one-shot: the next sign-in works
        assert "code" in code_from(http(auth_url(realm, challenge))[1]["Location"])

    def test_disabled_user_is_shown_an_error_page(self, realm):
        realm.disable_user("alice")
        _, challenge = pkce()
        realm.login_as("alice")
        status, headers, page = http(auth_url(realm, challenge))
        assert status == 200 and "Location" not in headers and b"Account is disabled" in page


class TestRefresh:
    def tokens(self, realm, user="alice"):
        code, verifier, _ = sign_in(realm, user)
        return exchange(realm, code, verifier)[1]

    def refresh(self, realm, rt):
        return token(realm, grant_type="refresh_token", refresh_token=rt)

    def test_refresh_returns_a_token_without_a_nonce_and_a_new_refresh_token(self, realm):
        t = self.tokens(realm)
        status, body = self.refresh(realm, t["refresh_token"])
        assert status == 200 and body["refresh_token"] != t["refresh_token"]
        assert "nonce" not in claims(body["id_token"])
        assert claims(body["id_token"])["sid"] == claims(t["id_token"])["sid"]
        assert self.refresh(realm, t["refresh_token"])[0] == 200  # revoke_refresh_token is off by default

    def test_unknown_refresh_token(self, realm):
        assert self.refresh(realm, "nope") == (400, {"error": "invalid_grant", "error_description": "Invalid refresh token"})

    def test_refresh_after_admin_logout_says_session_not_active(self, realm):
        t = self.tokens(realm)
        realm.admin_logout("alice")
        assert self.refresh(realm, t["refresh_token"])[1] == {"error": "invalid_grant",
                                                              "error_description": "Session not active"}

    def test_admin_logout_only_ends_that_users_sessions(self, realm):
        a, b = self.tokens(realm, "alice"), self.tokens(realm, "bob")
        realm.admin_logout("alice")
        assert self.refresh(realm, b["refresh_token"])[0] == 200
        assert self.refresh(realm, a["refresh_token"])[0] == 400

    def test_refresh_after_idle_timeout_says_token_is_not_active(self, realm):
        realm.set_realm(sso_session_idle_timeout=0.4)
        t = self.tokens(realm)
        time.sleep(0.6)
        assert self.refresh(realm, t["refresh_token"])[1] == {"error": "invalid_grant",
                                                              "error_description": "Token is not active"}

    def test_refreshing_keeps_the_session_from_going_idle(self, realm):
        realm.set_realm(sso_session_idle_timeout=0.8)
        rt = self.tokens(realm)["refresh_token"]
        for _ in range(3):  # 1.5 s in all: only the refreshes keep it alive
            time.sleep(0.5)
            status, body = self.refresh(realm, rt)
            assert status == 200
            rt = body["refresh_token"]

    def test_max_lifespan(self, realm):
        realm.set_realm(sso_session_max_lifespan=0.4)
        t = self.tokens(realm)
        time.sleep(0.6)
        assert self.refresh(realm, t["refresh_token"])[1]["error_description"] == "Token is not active"

    def test_disabled_user_cannot_refresh(self, realm):
        t = self.tokens(realm)
        realm.disable_user("alice")
        assert self.refresh(realm, t["refresh_token"])[0] == 400
        realm.enable_user("alice")
        assert self.refresh(realm, t["refresh_token"])[0] == 200

    def test_group_changes_show_up_in_the_next_token(self, realm):
        t = self.tokens(realm)
        realm.set_groups("alice", [])
        assert "groups" not in claims(self.refresh(realm, t["refresh_token"])[1]["id_token"])  # as Keycloak: absent, not []

    def test_refresh_token_rotation(self, realm):
        realm.set_realm(revoke_refresh_token=True)
        t = self.tokens(realm)
        status, body = self.refresh(realm, t["refresh_token"])
        assert status == 200
        assert self.refresh(realm, t["refresh_token"])[1]["error_description"] == \
            "Maximum allowed refresh token reuse exceeded"
        assert self.refresh(realm, body["refresh_token"])[0] == 200

    def test_end_session_endpoint_ends_the_session(self, realm):
        t = self.tokens(realm)
        status, _, _ = http(realm.issuer + "/protocol/openid-connect/logout?" +
                            urllib.parse.urlencode({"id_token_hint": t["id_token"]}))
        assert status == 200
        assert self.refresh(realm, t["refresh_token"])[1]["error_description"] == "Session not active"

    def test_userinfo_and_introspection_follow_the_session(self, realm):
        t = self.tokens(realm)
        auth = {"Authorization": "Bearer " + t["access_token"]}
        assert json.loads(http(realm.issuer + "/protocol/openid-connect/userinfo", headers=auth)[2])["preferred_username"] == "alice"
        realm.admin_logout("alice")
        assert http(realm.issuer + "/protocol/openid-connect/userinfo", headers=auth)[0] == 401
        # KC: token introspection is for confidential clients; ours is public
        status, _, body = http(realm.issuer + "/protocol/openid-connect/token/introspect", data={"token": t["access_token"]})
        assert (status, json.loads(body)) == (403, {"error": "invalid_request", "error_description": "Client not allowed."})


class TestKeys:
    def test_rotate_keeps_the_old_kid_until_retired(self, realm):
        old = realm.active_kid
        new = realm.rotate_keys()
        kids = lambda: [k["kid"] for k in realm.jwks()["keys"] if k["use"] == "sig"]  # noqa: E731
        assert new != old and kids() == [old, new]
        head = lambda jwt: json.loads(jc.b64u_decode(jwt.split(".")[0]))  # noqa: E731
        assert head(realm.mint("s", "n"))["kid"] == new          # new tokens use the new key
        assert head(realm.mint("s", "n", kid=old))["kid"] == old  # an old key can still sign (a laggard IdP node)
        realm.retire_key(old)
        assert kids() == [new]
        with pytest.raises(ValueError):
            realm.retire_key(new)

    @pytest.mark.realm(alg="ES256")
    def test_es256_realm_rotates_too(self, idp):
        old, new = idp.active_kid, idp.rotate_keys()
        assert old != new and len([k for k in idp.jwks()["keys"] if k["use"] == "sig"]) == 2

    def test_audience_mappers_and_group_paths(self):
        idp = MockKeycloak(audience_mapper=True, group_paths=True).start()
        try:
            idp.add_user("alice", "pw", groups=["admins", "/team/a"])
            code, verifier, _ = sign_in(idp)
            body = exchange(idp, code, verifier)[1]
            idt, at = claims(body["id_token"]), claims(body["access_token"])
            assert idt["aud"] == "vb-e2e"                      # the ID token's aud is the client id, as on Keycloak
            assert at["aud"] == ["vb-e2e", "account"]          # the Q1 gap: the access token now names our client
            assert idt["groups"] == ["/admins", "/team/a"] and idt["typ"] == "ID" and at["typ"] == "Bearer"
        finally:
            idp.stop()
        idp = MockKeycloak(extra_audience="billing-api").start()
        try:
            idp.add_user("alice", "pw")
            idp.login_as("alice")
            code, verifier, _ = sign_in(idp)
            body = exchange(idp, code, verifier)[1]
            assert claims(body["id_token"])["aud"] == ["vb-e2e", "billing-api"]
            assert claims(body["id_token"])["azp"] == "vb-e2e"
        finally:
            idp.stop()


class TestMint:
    def test_defaults_are_keycloak_shaped_and_overrides_apply(self, idp):
        c = claims(idp.mint("sub-1", "alice", claims={"email": "a@x"}, nonce="n"))
        assert c["typ"] == "ID" and c["azp"] == "vb-e2e" and c["sub"] == "sub-1" and c["email"] == "a@x"
        assert c["nonce"] == "n" and c["exp"] - c["iat"] == 300
        c = claims(idp.mint("s", "n", azp="other", typ="Bearer", aud=["a"], drop=["sid", "typ"]))
        assert c["azp"] == "other" and c["aud"] == ["a"] and "sid" not in c and "typ" not in c
        assert json.loads(jc.b64u_decode(idp.mint("s", "n", header_typ="at+jwt").split(".")[0]))["typ"] == "at+jwt"

    def test_real_backend_has_no_mint(self):
        from vbtest.idp import IdpBackend
        with pytest.raises(NotSupported):
            IdpBackend().mint()


# -- interactive login -----------------------------------------------------------------------------
class TestInteractiveLogin:
    @pytest.fixture
    def kc(self, idp):
        return idp

    @pytest.mark.realm(interactive=True)
    def test_browser_login_plays_the_form(self, kc):
        kc.add_user("alice", "pw")
        _, challenge = pkce()
        location = kc.browser_login(auth_url(kc, challenge), "alice", "pw")
        assert location.startswith(REDIRECT + "?") and "code" in code_from(location)
        assert [e["endpoint"] for e in kc.request_log()] == ["auth", "login"]

    @pytest.mark.realm(interactive=True)
    @pytest.mark.parametrize("user,password,message", [
        ("alice", "wrong", "Invalid username or password"), ("nobody", "pw", "Invalid username or password")])
    def test_wrong_credentials_are_refused(self, kc, user, password, message):
        kc.add_user("alice", "pw")
        with pytest.raises(LoginRefused, match=message):
            kc.browser_login(auth_url(kc, pkce()[1]), user, password)

    @pytest.mark.realm(interactive=True)
    def test_disabled_account_is_refused(self, kc):
        kc.add_user("bob", "pw", enabled=False)
        with pytest.raises(LoginRefused, match="Account is disabled"):
            kc.browser_login(auth_url(kc, pkce()[1]), "bob", "pw")

    @pytest.mark.realm(interactive=True)
    def test_the_form_needs_the_session_cookie(self, kc):
        kc.add_user("alice", "pw")
        _, _, page = http(auth_url(kc, pkce()[1]))
        action = html.unescape(re.search(r'action="([^"]+)"', page.decode()).group(1))
        status, _, page = http(action, data={"username": "alice", "password": "pw"})  # no cookie
        assert status == 400 and b"Cookie not found" in page

    @pytest.mark.realm(interactive=True)
    def test_redirect_uri_mismatch_is_refused(self, kc):
        with pytest.raises(LoginRefused, match="redirect_uri"):
            kc.browser_login(auth_url(kc, pkce()[1], redirect_uri="https://evil.example/cb"), "a", "b")

    def test_cancelled_login_raises(self, idp):
        idp.add_user("alice", "pw")
        idp.cancel_logins("access_denied", "User cancelled")
        with pytest.raises(LoginRefused, match="access_denied"):
            idp.browser_login(auth_url(idp, pkce()[1]), "alice", "pw")

    def test_obtain_tokens_non_interactive(self, realm):
        t = realm.obtain_tokens("alice", "pw")
        assert claims(t["id_token"])["preferred_username"] == "alice"


# -- K2.3 faults -----------------------------------------------------------------------------------
class TestFaults:
    def test_status_fault_counts_down_and_is_logged(self, idp):
        idp.faults.status("certs", 503, times=2)
        url = idp.issuer + "/protocol/openid-connect/certs"
        assert [http(url)[0] for _ in range(3)] == [503, 503, 200]
        log = idp.requests("certs")
        assert [e["status"] for e in log] == [503, 503, 200]
        assert [e["fault"] for e in log] == ["status=503", "status=503", None]

    def test_malformed_and_oversized_bodies(self, idp):
        url = idp.issuer + "/protocol/openid-connect/certs"
        idp.faults.body("certs", b"{", times=1)
        status, _, body = http(url)
        assert status == 200 and body == b"{"
        idp.faults.body("certs", b"x" * (2 << 20), times=1)
        assert len(http(url)[2]) == 2 << 20
        assert http(url)[0] == 200 and json.loads(http(url)[2])["keys"]

    def test_latency(self, idp):
        idp.faults.latency("discovery", 0.5)
        t0 = time.time()
        http(idp.issuer + "/.well-known/openid-configuration")
        assert time.time() - t0 >= 0.5
        assert "latency=0.5s" in idp.requests("discovery")[0]["fault"]
        idp.faults.clear("discovery")
        t0 = time.time()
        http(idp.issuer + "/.well-known/openid-configuration")
        assert time.time() - t0 < 0.4

    def test_drop_closes_the_socket_without_an_answer(self, idp):
        idp.faults.drop("token", times=1)
        with pytest.raises((urllib.error.URLError, ConnectionError, OSError)):
            http(idp.issuer + "/protocol/openid-connect/token", data={"grant_type": "x", "client_id": "vb-e2e"})
        entry = idp.requests("token")[0]
        assert entry["status"] == "dropped" and entry["fault"] == "drop"
        assert http(idp.issuer + "/protocol/openid-connect/token", data={"grant_type": "x", "client_id": "vb-e2e"})[0] == 400

    def test_discovery_issuer_mismatch(self, idp):
        idp.faults.discovery_issuer("https://evil.example/realms/x")
        assert jget(idp.issuer + "/.well-known/openid-configuration")[1]["issuer"] == "https://evil.example/realms/x"
        idp.faults.clear()
        assert jget(idp.issuer + "/.well-known/openid-configuration")[1]["issuer"] == idp.issuer

    def test_unknown_endpoint_is_an_error_in_the_test_not_a_silent_noop(self, idp):
        with pytest.raises(ValueError):
            idp.faults.status("tokens", 500)

    def test_a_fault_on_one_endpoint_leaves_the_others_alone(self, idp):
        idp.faults.status("token", 500)
        assert http(idp.issuer + "/protocol/openid-connect/certs")[0] == 200


# -- K1.10 request log -----------------------------------------------------------------------------
class TestRequestLog:
    def test_secrets_are_shortened_to_four_characters(self, realm):
        code, verifier, _ = sign_in(realm)
        exchange(realm, code, verifier)
        entry = realm.requests("token")[0]
        assert entry["params"]["code"] == code[:4] + "..." and entry["params"]["code_verifier"] == verifier[:4] + "..."
        assert entry["params"]["client_id"] == "vb-e2e" and entry["status"] == 200
        assert realm.token_requests[0]["code_verifier"] == verifier  # the unredacted copy tests may assert on
        assert realm.grants() == ["authorization_code"]

    def test_log_entries_are_copies(self, idp):
        http(idp.issuer + "/.well-known/openid-configuration")
        idp.request_log()[0]["status"] = "tampered"
        assert idp.request_log()[0]["status"] == 200


def test_stdlib_only():
    """The emulator must import with nothing but the standard library (the harness needs only pytest)."""
    import subprocess
    import sys
    import pathlib
    root = pathlib.Path(__file__).resolve().parent
    out = subprocess.run([sys.executable, "-I", "-c", "import sys; sys.path.insert(0, %r); "
                          "import vbtest.mock_keycloak, vbtest.idp; "
                          "bad = [m for m in sys.modules if m.split('.')[0] in ('pytest', '_pytest', 'cryptography', 'jwt')]; "
                          "assert not bad, bad" % str(root)], capture_output=True, text=True)
    assert out.returncode == 0, out.stderr


def test_a_thread_per_request_server_survives_parallel_clients(idp):
    results = []

    def hit():
        results.append(http(idp.issuer + "/protocol/openid-connect/certs")[0])

    threads = [threading.Thread(target=hit) for _ in range(20)]
    [t.start() for t in threads]
    [t.join() for t in threads]
    assert results == [200] * 20 and len(idp.request_log()) == 20
