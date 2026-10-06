"""Writes the golden Keycloak fixtures the C++ unit tests read (tests/unit/fixtures/keycloak/).

    python3 -m vbtest.mock_keycloak --dump-fixtures tests/unit/fixtures/keycloak     # from tests/e2e

Deterministic: a fixed clock, seeded ids, RFC 6979 / PKCS#1 v1.5 signatures and the committed
test-only keys, so a re-run changes nothing unless the emulator changed. test_mock_keycloak_matches_real.py
(K5.3) re-generates these from a real Keycloak and fails with a diff when they drift.
"""
import hashlib
import json
import pathlib
import urllib.error
import urllib.parse
import urllib.request

from . import jwscrypto as jc
from .mock_keycloak import MockKeycloak

NOW = 1_800_000_000
ISSUER_BASE = "https://keycloak.example.test"
NONCE = "fixture-nonce"
REDIRECT = "http://127.0.0.1:5555/callback"
VERIFIER = jc.b64u(b"fixture-verifier-fixture-verifier"[:32])


class _NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, *a, **kw):
        return None


def _http(url, data=None):
    req = urllib.request.Request(url, data=urllib.parse.urlencode(data).encode() if data is not None else None)
    try:
        with urllib.request.build_opener(_NoRedirect).open(req, timeout=10) as r:
            return r.status, dict(r.headers), r.read()
    except urllib.error.HTTPError as e:
        return e.code, dict(e.headers), e.read()


class Realm:
    """A MockKeycloak on a clock the dump controls, with helpers that go through real HTTP."""

    def __init__(self, **kw):
        self.t = NOW
        self.idp = MockKeycloak(clock=lambda: self.t, seed=7, issuer_base=ISSUER_BASE, **kw).start()
        self.idp.add_user("alice", "pw", email="alice@example.test", groups=["admins", "players"], roles=["moderator"])
        self.idp.add_user("bob", "pw", email="bob@example.test")

    def oidc(self, leaf):
        return self.idp.local_url + "/protocol/openid-connect/" + leaf

    def code_flow(self, user="alice"):
        self.idp.login_as(user)
        challenge = jc.b64u(hashlib.sha256(VERIFIER.encode()).digest())
        q = {"client_id": self.idp.client_id, "response_type": "code", "scope": "openid", "redirect_uri": REDIRECT,
             "state": "fixture-state", "nonce": NONCE, "code_challenge": challenge, "code_challenge_method": "S256"}
        _, headers, _ = _http(self.oidc("auth?") + urllib.parse.urlencode(q))
        return dict(urllib.parse.parse_qsl(urllib.parse.urlparse(headers["Location"]).query))["code"]

    def exchange(self, code, verifier=VERIFIER):
        return _http(self.oidc("token"), {"grant_type": "authorization_code", "client_id": self.idp.client_id,
                                          "code": code, "redirect_uri": REDIRECT, "code_verifier": verifier})

    def tokens(self, user="alice"):
        status, _, body = self.exchange(self.code_flow(user))
        assert status == 200, body
        return json.loads(body)

    def refresh(self, rt):
        return _http(self.oidc("token"), {"grant_type": "refresh_token", "client_id": self.idp.client_id,
                                          "refresh_token": rt})

    def get(self, leaf):
        status, _, body = _http(self.idp.local_url + "/" + leaf)
        assert status == 200, (leaf, status)
        return json.loads(body)

    def stop(self):
        self.idp.stop()


def dump(out):
    out = pathlib.Path(out)
    (out / "errors").mkdir(parents=True, exist_ok=True)

    def write(rel, doc):
        (out / rel).write_text(json.dumps(doc, indent=2) + "\n")

    notes = []
    rs = Realm()
    try:
        write("discovery.json", rs.get(".well-known/openid-configuration"))
        write("jwks_rs256.json", rs.get("protocol/openid-connect/certs"))
        first = rs.tokens()
        write("token_response.json", first)
        status, _, body = rs.refresh(first["refresh_token"])
        refreshed = json.loads(body)
        write("refresh_response.json", refreshed)

        tokens = {
            "id_rs256": ("code-flow ID token, RS256, string aud", first["id_token"]),
            "id_refresh_no_nonce": ("ID token from a refresh grant: no nonce claim", refreshed["id_token"]),
            "access_token": ("the access token of the same response (typ Bearer, aud account)", first["access_token"]),
        }
        mint = rs.idp.mint
        tokens.update({
            "id_azp_other": ("azp names another client", mint("sub-1", "alice", azp="other-client", nonce=NONCE)),
            "id_aud_without_client": ("aud array that omits our client", mint("sub-1", "alice", aud=["account", "other"], nonce=NONCE)),
            "id_typ_bearer": ("typ claim Bearer", mint("sub-1", "alice", typ="Bearer", nonce=NONCE)),
            "id_typ_logout": ("typ claim Logout", mint("sub-1", "alice", typ="Logout", nonce=NONCE)),
            "id_typ_missing": ("no typ claim at all", mint("sub-1", "alice", drop=["typ"], nonce=NONCE)),
            "id_header_at_jwt": ("otherwise valid, but the JWS header typ is at+jwt", mint("sub-1", "alice", header_typ="at+jwt", nonce=NONCE)),
            "id_issuer_trailing_slash": ("iss has a trailing slash", mint("sub-1", "alice", iss=rs.idp.issuer + "/", nonce=NONCE)),
            "id_many_groups": ("300 full-path groups: claims must stay under the cap or be refused whole",
                               mint("sub-1", "alice", nonce=NONCE, claims={"groups": ["/organisation/department-%03d" % i for i in range(300)]})),
        })
        rotated_from = rs.idp.active_kid
        rs.idp.rotate_keys()
        tokens["id_new_key"] = ("signed by the key that became active after rotation", mint("sub-1", "alice", nonce=NONCE))
        tokens["id_old_key_after_rotation"] = ("signed by the previous key (kid %s), as an IdP node that lags" % rotated_from,
                                               mint("sub-1", "alice", nonce=NONCE, kid=rotated_from))
        write("jwks_rotated.json", rs.get("protocol/openid-connect/certs"))
        rs.idp.retire_key(rotated_from)
        write("jwks_retired.json", rs.get("protocol/openid-connect/certs"))
        notes.append("kids: before rotation `%s`, after `%s`" % (rotated_from, rs.idp.active_kid))

        # -- error bodies, each triggered the way a client would trigger it ---------------------
        def err(name, response):
            status, _, body = response
            doc = json.loads(body)
            write("errors/%s.json" % name, {"status": status, "body": doc})

        err("code_not_valid", rs.exchange(rs.code_flow()[:-1] + "x"))
        err("pkce_failed", rs.exchange(rs.code_flow(), verifier="x" * 43))
        err("invalid_client", _http(rs.oidc("token"), {"grant_type": "refresh_token", "client_id": "nope", "refresh_token": "x"}))
        err("invalid_refresh_token", rs.refresh("not-a-token"))
        t = rs.tokens("bob")
        rs.idp.disable_user("bob")
        err("user_disabled", rs.refresh(t["refresh_token"]))
        rs.idp.enable_user("bob")
        t = rs.tokens("alice")
        rs.idp.admin_logout("alice")
        err("session_not_active", rs.refresh(t["refresh_token"]))
        t = rs.tokens("alice")
        rs.t += rs.idp.settings["sso_session_idle_timeout"] + 1
        err("token_not_active", rs.refresh(t["refresh_token"]))
        rs.t = NOW
        rs.idp.set_realm(revoke_refresh_token=True)
        t = rs.tokens("alice")
        rs.refresh(t["refresh_token"])
        err("refresh_reuse_exceeded", rs.refresh(t["refresh_token"]))
        err("unsupported_grant_type", _http(rs.oidc("token"), {"grant_type": "password", "client_id": rs.idp.client_id}))
    finally:
        rs.stop()

    ec = Realm(alg="ES256")
    try:
        write("jwks_es256.json", ec.get("protocol/openid-connect/certs"))
        tokens["id_es256"] = ("code-flow ID token, ES256 realm", ec.tokens()["id_token"])
    finally:
        ec.stop()

    au = Realm(audience_mapper=True, group_paths=True)
    try:
        t = au.tokens()
        tokens["id_full_path_groups"] = ("full-path groups (/admins, /players); the ID token's aud is still just the client id", t["id_token"])
        tokens["access_token_with_audience"] = ("audience mapper for our own client: the access token's aud is [client, account] "
                                                "and azp is ours, so only the typ rule stops it", t["access_token"])
    finally:
        au.stop()
    ex = Realm(extra_audience="billing-api")
    try:
        tokens["id_audience_array"] = ("a mapper adds another audience: aud is [client, billing-api], azp is the client",
                                       ex.tokens()["id_token"])
    finally:
        ex.stop()

    write("tokens.json", {"now": NOW, "issuer": ISSUER_BASE + "/realms/e2e", "client_id": "vb-e2e", "nonce": NONCE,
                          "tokens": {k: {"note": v[0], "token": v[1]} for k, v in tokens.items()}})
    (out / "fixtures.md").write_text(FIXTURES_MD.format(now=NOW, issuer=ISSUER_BASE + "/realms/e2e", notes="\n".join("- " + n for n in notes)))
    print("wrote fixtures to", out)


FIXTURES_MD = """# Keycloak golden fixtures

Generated, do not edit by hand:

    cd tests/e2e && python3 -m vbtest.mock_keycloak --dump-fixtures ../unit/fixtures/keycloak

by `vbtest/fixture_dump.py` from the `MockKeycloak` emulator with a fixed clock (`now = {now}`), seeded
ids and the committed TEST-ONLY keys in `tests/e2e/fixtures/`. Re-running it changes nothing unless the
emulator changed. The real-Keycloak workflow (`auth_keycloak.yml`, K5.3) regenerates them from a real
Keycloak into a temp directory and fails with a diff when they differ in shape, which is what keeps
these honest. Tokens expire, so the unit tests pass `now` from `tokens.json` to `verify_id_token`.

| File | What |
|---|---|
| `discovery.json` | `/.well-known/openid-configuration` of realm `e2e` (issuer `{issuer}`) |
| `jwks_rs256.json` | the realm's keys: one RS256 signing key and the RSA-OAEP `enc` key the engine must skip |
| `jwks_es256.json` | the same for an ES256 realm |
| `jwks_rotated.json` | after a key rotation: old and new signing key |
| `jwks_retired.json` | after the old key was retired: only the new one |
| `token_response.json`, `refresh_response.json` | token endpoint answers (code flow, refresh grant) |
| `errors/*.json` | `{{status, body}}` of every error the client maps (K1.5) |
| `tokens.json` | ID tokens with known claims, signed with the keys above, each with a note |

{notes}
"""
