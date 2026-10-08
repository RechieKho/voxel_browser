"""The emulator against a real Keycloak (docs/auth-keycloak-testing.md, K5.3). Opt-in: `--vb-idp=keycloak`.

The emulator exists so the engine can be tested against Keycloak-shaped data without Keycloak. That is
only worth anything while it stays *honest*, so this compares what both produce for the same steps:
discovery, JWKS, the token response, ID and access token claims, a refresh-derived token, and every
error body the client maps. Rules: what the emulator says, the real server must say too (same keys,
same JSON types; same status, `error` and `error_description` for errors). A real Keycloak may say
more (it has given_name, x5c, frontchannel_logout_supported, ...); it may not say *less*, and it may
not say something else. Values that are ids, times or secrets are not compared.

When a case here fails, fix `vbtest/mock_keycloak.py` (and regenerate the unit fixtures with
`python3 -m vbtest.mock_keycloak --dump-fixtures tests/unit/fixtures/keycloak`), not the test.
"""
import json
import time
import urllib.error
import urllib.parse
import urllib.request

import pytest

from vbtest import jwscrypto as jc
from vbtest.mock_keycloak import MockKeycloak

pytestmark = pytest.mark.keycloak_real

REDIRECT = "http://127.0.0.1:5555/callback"


# -- helpers -------------------------------------------------------------------------------------------
def kind(v):
    return "bool" if isinstance(v, bool) else "num" if isinstance(v, (int, float)) else "str" if isinstance(v, str) \
        else "null" if v is None else "list" if isinstance(v, list) else "object"


def assert_covers(emulated, real, path="$"):
    """Everything in `emulated` exists in `real` with the same JSON type (recursively)."""
    assert kind(emulated) == kind(real), "%s: emulator says %s, Keycloak says %s (%r)" % (path, kind(emulated), kind(real), real)
    if isinstance(emulated, dict):
        missing = sorted(set(emulated) - set(real))
        assert not missing, "%s: Keycloak has no %s (the emulator invented them)" % (path, ", ".join(missing))
        for k in emulated:
            assert_covers(emulated[k], real[k], "%s.%s" % (path, k))
    elif isinstance(emulated, list) and emulated and real:
        assert_covers(emulated[0], real[0], path + "[0]")


def http(url, data=None, headers=None):
    class NoRedirect(urllib.request.HTTPRedirectHandler):
        def redirect_request(self, *a, **kw):
            return None

    req = urllib.request.Request(url, data=urllib.parse.urlencode(data).encode() if data is not None else None,
                                 headers=headers or {})
    try:
        with urllib.request.build_opener(NoRedirect).open(req, timeout=30) as r:
            return r.status, r.read()
    except urllib.error.HTTPError as e:
        return e.code, e.read()


def jwt_parts(token):
    head, body, _ = token.split(".")
    return json.loads(jc.b64u_decode(head)), json.loads(jc.b64u_decode(body))


class Flow:
    """Backend-neutral raw OIDC calls (no engine involved)."""

    def __init__(self, backend):
        self.b = backend
        self.verifier = jc.b64u(b"v" * 32)
        self.challenge = jc.b64u(__import__("hashlib").sha256(self.verifier.encode()).digest())

    def oidc(self, leaf):
        return self.b.local_url + "/protocol/openid-connect/" + leaf

    def auth_url(self, **over):
        q = {"client_id": self.b.client_id, "response_type": "code", "scope": "openid", "redirect_uri": REDIRECT,
             "state": "st", "nonce": "n-1", "code_challenge": self.challenge, "code_challenge_method": "S256"}
        q.update(over)
        return self.oidc("auth?") + urllib.parse.urlencode(q)

    def code(self, user="alice"):
        location = self.b.browser_login(self.auth_url(), user, "pw")
        return dict(urllib.parse.parse_qsl(urllib.parse.urlparse(location).query))["code"]

    def exchange(self, code, verifier=None, **over):
        form = {"grant_type": "authorization_code", "client_id": self.b.client_id, "code": code,
                "redirect_uri": REDIRECT, "code_verifier": verifier or self.verifier}
        form.update(over)
        status, body = http(self.oidc("token"), form)
        return status, json.loads(body)

    def tokens(self, user="alice"):
        status, body = self.exchange(self.code(user))
        assert status == 200, body
        return body

    def refresh(self, rt, **over):
        form = {"grant_type": "refresh_token", "client_id": self.b.client_id, "refresh_token": rt}
        form.update(over)
        status, body = http(self.oidc("token"), form)
        return status, json.loads(body)


@pytest.fixture
def both(idp):
    """(emulator, real) with the same user; `idp` is the real one under --vb-idp=keycloak."""
    mock = MockKeycloak(interactive=True).start()
    for b in (mock, idp):
        b.add_user("alice", "pw", email="alice@example.com", groups=["admins"], roles=["moderator"])
        b.add_user("bob", "pw")
    yield Flow(mock), Flow(idp)
    mock.stop()


# -- the documents --------------------------------------------------------------------------------------
def test_discovery_document(both):
    mock, real = both
    m = json.loads(http(mock.b.issuer + "/.well-known/openid-configuration")[1])
    r = json.loads(http(real.b.issuer + "/.well-known/openid-configuration")[1])
    assert_covers(m, r)
    for field in ("issuer", "authorization_endpoint", "token_endpoint", "jwks_uri", "end_session_endpoint",
                  "introspection_endpoint", "userinfo_endpoint"):   # same layout, whatever the host
        assert urllib.parse.urlparse(m[field]).path == urllib.parse.urlparse(r[field]).path, field
    for field in ("code_challenge_methods_supported", "grant_types_supported", "response_types_supported",
                  "id_token_signing_alg_values_supported", "subject_types_supported"):
        assert set(m[field]) <= set(r[field]), "%s: %s" % (field, sorted(set(m[field]) - set(r[field])))
    assert r["issuer"] == real.b.issuer


def test_jwks(both):
    mock, real = both
    m = json.loads(http(mock.oidc("certs"))[1])["keys"]
    r = json.loads(http(real.oidc("certs"))[1])["keys"]
    for use in ("sig", "enc"):
        em = next(k for k in m if k["use"] == use)
        rl = next(k for k in r if k["use"] == use)
        assert_covers(em, rl)
        assert {k: em[k] for k in ("kty", "alg", "use")} == {k: rl[k] for k in ("kty", "alg", "use")}


# -- the tokens ---------------------------------------------------------------------------------------
def test_token_response_and_claims(both):
    mock, real = both
    m, r = mock.tokens(), real.tokens()
    assert set(m) == set(r), (sorted(m), sorted(r))
    assert_covers({k: v for k, v in m.items() if not k.endswith("token")}, {k: v for k, v in r.items() if not k.endswith("token")})
    for name in ("id_token", "access_token"):
        mh, mc = jwt_parts(m[name])
        rh, rc = jwt_parts(r[name])
        assert mh.keys() == rh.keys() and mh["alg"] == rh["alg"] and mh["typ"] == rh["typ"]
        assert_covers(mc, rc)
        assert mc["typ"] == rc["typ"] and mc["azp"] == rc["azp"] and mc["acr"] == rc["acr"]
        assert (mc["aud"] if name == "id_token" else mc["aud"]) == rc["aud"]
        assert mc["realm_access"]["roles"] and sorted(mc["realm_access"]["roles"]) == sorted(rc["realm_access"]["roles"])
    _, mi = jwt_parts(m["id_token"])
    _, ri = jwt_parts(r["id_token"])
    assert mi["groups"] == ri["groups"] == ["admins"]
    assert mi["at_hash"] == jc.b64u(__import__("hashlib").sha256(m["access_token"].encode()).digest()[:16])
    assert ri["at_hash"] == jc.b64u(__import__("hashlib").sha256(r["access_token"].encode()).digest()[:16])
    assert mi["nonce"] == ri["nonce"] == "n-1"
    # Real Keycloak computes expires_in as exp - now in whole seconds, so a second
    # boundary crossed while it issues the token reports 299.
    assert m["expires_in"] == 300 and r["expires_in"] in (299, 300), (m["expires_in"], r["expires_in"])


def test_refresh_derived_token_has_no_nonce(both):
    mock, real = both
    outs = []
    for flow in (mock, real):
        t = flow.tokens()
        status, body = flow.refresh(t["refresh_token"])
        assert status == 200
        _, claims = jwt_parts(body["id_token"])
        assert "nonce" not in claims
        assert body["refresh_token"] != t["refresh_token"]
        assert claims["sid"] == jwt_parts(t["id_token"])[1]["sid"]
        outs.append((set(body), claims))
    assert outs[0][0] == outs[1][0]
    assert_covers(outs[0][1], outs[1][1])


def test_a_user_in_no_group_has_no_groups_claim(both):
    mock, real = both
    for flow in (mock, real):
        _, claims = jwt_parts(flow.tokens("bob")["id_token"])
        assert "groups" not in claims


def test_group_paths_option(idp):
    """Emulator `group_paths=True` and the realm's full-path mapper agree."""
    try:
        idp.reset(interactive=True, group_paths=True)
    except Exception as e:  # NotSupported on a backend that cannot
        pytest.skip(str(e))
    idp.add_user("alice", "pw", groups=["admins"])
    mock = MockKeycloak(interactive=True, group_paths=True).start()
    try:
        mock.add_user("alice", "pw", groups=["admins"])
        assert jwt_parts(Flow(idp).tokens()["id_token"])[1]["groups"] == jwt_parts(Flow(mock).tokens()["id_token"])[1]["groups"] == ["/admins"]
    finally:
        mock.stop()


@pytest.mark.realm(audience_mapper=True)
def test_audience_mapper_for_our_own_client(idp):
    """The Q1 case: our client in the *access* token's aud; the ID token's aud stays the client id."""
    mock = MockKeycloak(interactive=True, audience_mapper=True).start()
    try:
        for b in (mock, idp):
            b.add_user("alice", "pw")
        m, r = Flow(mock).tokens(), Flow(idp).tokens()
        assert jwt_parts(m["id_token"])[1]["aud"] == jwt_parts(r["id_token"])[1]["aud"] == idp.client_id
        assert jwt_parts(m["access_token"])[1]["aud"] == jwt_parts(r["access_token"])[1]["aud"] == [idp.client_id, "account"]
    finally:
        mock.stop()


@pytest.mark.realm(extra_audience="billing-api")
def test_audience_mapper_for_another_audience(idp):
    mock = MockKeycloak(interactive=True, extra_audience="billing-api").start()
    try:
        for b in (mock, idp):
            b.add_user("alice", "pw")
        m, r = Flow(mock).tokens(), Flow(idp).tokens()
        assert jwt_parts(m["id_token"])[1]["aud"] == jwt_parts(r["id_token"])[1]["aud"] == [idp.client_id, "billing-api"]
        assert sorted(jwt_parts(m["access_token"])[1]["aud"]) == sorted(jwt_parts(r["access_token"])[1]["aud"])
        assert jwt_parts(r["id_token"])[1]["azp"] == idp.client_id
    finally:
        mock.stop()


# -- the errors -----------------------------------------------------------------------------------------
def _error_code_not_valid(f):
    code = f.code()
    f.exchange(code)
    return f.exchange(code)


def _error_pkce(f):
    return f.exchange(f.code(), verifier="w" * 43)


def _error_pkce_bad_format(f):
    return f.exchange(f.code(), verifier="x")


def _error_pkce_missing(f):
    status, body = http(f.oidc("token"), {"grant_type": "authorization_code", "client_id": f.b.client_id,
                                          "code": f.code(), "redirect_uri": REDIRECT})
    return status, json.loads(body)


def _error_redirect_uri_at_token(f):
    return f.exchange(f.code(), redirect_uri="http://127.0.0.1:1/other")


def _error_no_grant_type(f):
    status, body = http(f.oidc("token"), {"client_id": f.b.client_id})
    return status, json.loads(body)


def _error_no_refresh_token(f):
    status, body = http(f.oidc("token"), {"grant_type": "refresh_token", "client_id": f.b.client_id})
    return status, json.loads(body)


def _error_wrong_client(f):
    return f.exchange(f.code(), client_id="someone-else")


def _error_unknown_refresh_token(f):
    return f.refresh("not-a-token")


def _error_disabled_user(f):
    t = f.tokens("bob")
    f.b.disable_user("bob")
    try:
        return f.refresh(t["refresh_token"])
    finally:
        f.b.enable_user("bob")


def _error_logged_out(f):
    t = f.tokens("alice")
    f.b.admin_logout("alice")
    return f.refresh(t["refresh_token"])


def _error_idle(f):
    f.b.set_realm(sso_session_idle_timeout=2)
    t = f.tokens("alice")
    time.sleep(3.5)
    try:
        return f.refresh(t["refresh_token"])
    finally:
        f.b.set_realm(sso_session_idle_timeout=1800)


def _error_refresh_reuse(f):
    f.b.set_realm(revoke_refresh_token=True)
    try:
        t = f.tokens("alice")
        f.refresh(t["refresh_token"])
        return f.refresh(t["refresh_token"])
    finally:
        f.b.set_realm(revoke_refresh_token=False)


def _error_unsupported_grant(f):
    status, body = http(f.oidc("token"), {"grant_type": "bogus", "client_id": f.b.client_id})
    return status, json.loads(body)


@pytest.mark.parametrize("scenario", [
    _error_code_not_valid, _error_pkce, _error_pkce_bad_format, _error_pkce_missing, _error_wrong_client,
    _error_redirect_uri_at_token, _error_no_grant_type, _error_no_refresh_token, _error_unknown_refresh_token, _error_disabled_user,
    _error_logged_out, _error_idle, _error_refresh_reuse, _error_unsupported_grant],
    ids=lambda f: f.__name__[len("_error_"):])
def test_error_body(both, scenario):
    mock, real = both
    m, r = scenario(mock), scenario(real)
    assert (m[0], m[1].get("error"), m[1].get("error_description")) == (r[0], r[1].get("error"), r[1].get("error_description")), \
        "emulator %r  vs  Keycloak %r" % (m, r)


def test_redirect_uri_not_matching_is_a_400_page(both):
    mock, real = both
    for flow in (mock, real):
        status, page = http(flow.auth_url(redirect_uri="https://evil.example/cb"))
        assert status == 400 and b"Invalid parameter: redirect_uri" in page, (status, page[:200])
    for flow in (mock, real):  # RFC 8252: any loopback port matches `http://127.0.0.1/*`
        assert http(flow.auth_url(redirect_uri="http://127.0.0.1:1/x"))[0] == 200  # the login form


def test_login_form_messages(both):
    from vbtest.idp import LoginRefused
    mock, real = both
    for flow in (mock, real):
        flow.b.add_user("dave", "pw", enabled=False)
        with pytest.raises(LoginRefused) as wrong:
            flow.b.browser_login(flow.auth_url(), "alice", "nope")
        with pytest.raises(LoginRefused) as disabled:
            flow.b.browser_login(flow.auth_url(), "dave", "pw")
        flow.messages = (str(wrong.value), str(disabled.value))
    assert mock.messages == real.messages, (mock.messages, real.messages)
