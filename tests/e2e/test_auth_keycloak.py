"""Authentication against a Keycloak (docs/auth-keycloak-testing.md, K4).

Real server, real headless clients, and an identity provider behind the `IdpBackend` interface:
the MockKeycloak emulator by default, a real Keycloak with `--vb-idp=keycloak`. Tests use only
that interface, so the same file runs on both; a test that needs `mint()` or fault injection is
marked `mock_only`. `test_auth.py` stays as the quick smoke suite.

How these tests drive time: the periodic re-auth (auth.md 5.6) has a 60 s minimum interval, so
instead of waiting, the server's automation command `advance_reauth` fast-forwards the tick-time
timers and the *real* `system_reauth` path runs (the one wall-clock test is in test_auth.py).
How they sign in: `obtain_tokens` yields a token-file token through the real code flow; tests that
need a refresh token (re-auth against the IdP) play the browser with `BrowserPlayer` instead, so the
client's saved sign-in is exactly what a player would have.
"""
import json
import threading
import time
import urllib.error
import urllib.parse
import urllib.request

import pytest

from vbtest import ProcessDied, expect
from vbtest.idp import BrowserPlayer, auth_lua, eventually
from vbtest.mock_keycloak import MockKeycloak
from vbtest.stack import make_pack

# The login form is what real Keycloak shows; the emulator serves one when asked.
pytestmark = pytest.mark.realm(interactive=True)

REPO = __import__("pathlib").Path(__file__).resolve().parents[2]
FAST_REAUTH = dict(reauth_interval=60, reauth_grace=120)   # never waited on: advance_reauth moves the timers


# -- helpers -----------------------------------------------------------------------------------------
@pytest.fixture
def realm(idp):
    """The users every scenario below can sign in as (password "pw")."""
    for name in ("alice", "bob", "carl", "dave", "erin", "frank"):
        idp.add_user(name, "pw", email=name + "@example.com")
    return idp


@pytest.fixture
def browser_join(monkeypatch, tmp_path, make_clients):
    """`browser_join(idp, server, "alice")`: a headless client signs in through the real browser flow
    (this test plays the browser), so it ends up with a saved refresh token. Returns (client, player).

    `outcome` says what the client is expected to do: "joins" (default); "dies" (the sign-in is
    abandoned and the client exits with its error: ProcessDied is swallowed, client is None);
    "stuck" (the browser never completes, so the client just waits: it is started on a thread and
    left for the fixture teardown to kill, client is None)."""
    def join(idp, server, user, password="pw", home=None, tag=None, outcome="joins"):
        url_file = tmp_path / ("auth-url-%s.txt" % (tag or user))
        monkeypatch.setenv("VB_AUTH_URL_FILE", str(url_file))
        player = BrowserPlayer(idp, url_file, user, password).start()
        factory = make_clients(server)
        if outcome == "stuck":
            def spawn():
                try:
                    factory(1, home=home or user)
                except Exception:
                    pass          # killed by the teardown while it still waited
            threading.Thread(target=spawn, daemon=True).start()
            return None, player
        try:
            (c,) = factory(1, home=home or user)
        except ProcessDied:
            if outcome != "dies":
                raise
            return None, player
        player.join()
        return c, player
    return join


def reauth_round(server, name, done_before):
    """Make the server ask `name` to prove their login again now, and wait for the accepted answer."""
    server.advance_reauth(name, 1_000_000)
    expect(server).to_have_logged("re-auth ok for '%s'" % name, count=done_before + 1, timeout=30)


def refresh_grants(idp):
    return idp.grants().count("refresh_token")


# =========================================================================================================
# K4.3 sign-in scenarios
# =========================================================================================================
def test_browser_login_with_password(realm, auth_server, browser_join):
    server = auth_server()
    alice, player = browser_join(realm, server, "alice")
    expect(server).to_have_player_login(realm.user_id("alice"), name="alice", provider="keycloak")
    assert "Signed in" in player.page
    browser = [e["endpoint"] for e in realm.request_log() if e["endpoint"] in ("auth", "login")]
    assert browser == ["auth", "login"], browser                   # the form was shown, then a POST
    assert [e["status"] for e in realm.requests("login")] == [302]
    token = realm.requests("token")[0]
    assert token["params"]["grant_type"] == "authorization_code" and token["params"]["code_verifier"]
    assert realm.requests("auth")[0]["params"]["code_challenge_method"] == "S256"


@pytest.mark.mock_only          # a real Keycloak's login form has no "cancel": it cannot be made to say access_denied
def test_user_cancels_at_the_idp(realm, auth_server, browser_join):
    server = auth_server()
    realm.cancel_logins("access_denied", "User cancelled")
    _, player = browser_join(realm, server, "alice", outcome="dies")   # a headless client has no screen to retry on
    player.join()
    assert "access_denied" in str(player.refused)
    expect(server).to_have_player_count(0)
    assert realm.requests("token") == [] and realm.requests("login") == []   # no code was ever issued
    expect(server).not_.to_have_logged("sign-in rejected", timeout=1)     # C2S_Auth never reached the server


def test_disabled_user_cannot_sign_in(realm, auth_server, browser_join):
    server = auth_server()
    realm.disable_user("bob")
    _, player = browser_join(realm, server, "bob", outcome="stuck")
    player.join(raises=False)
    assert player.refused is not None and "disabled" in str(player.refused).lower(), (player.refused, player.error)
    expect(server).to_have_player_count(0)
    assert realm.requests("token") == []                           # no code, so nothing to exchange


def test_disabled_user_with_a_still_valid_token_is_not_the_servers_business(realm, auth_server, make_clients,
                                                                           write_token):
    """The server only sees tokens: one issued before the account was disabled keeps working until
    it expires (<= max_token_age_seconds), which is exactly what periodic re-auth exists to bound."""
    server = auth_server()
    tokens = realm.obtain_tokens("bob", "pw")
    realm.disable_user("bob")
    f = write_token(tokens["id_token"])
    make_clients(server)(1, extra_args=["--auth-token-file", str(f)])
    expect(server).to_have_player_login(realm.user_id("bob"), name="bob")


@pytest.mark.realm(alg="ES256")
def test_es256_realm(realm, auth_server, make_clients, write_token):
    server = auth_server()
    f = write_token(realm.obtain_tokens("alice", "pw")["id_token"])
    make_clients(server)(1, extra_args=["--auth-token-file", str(f)])
    expect(server).to_have_player_login(realm.user_id("alice"), name="alice")
    assert len(realm.requests("certs")) == 1                       # one JWKS fetch served the join


@pytest.mark.realm(extra_audience="billing-api")
def test_audience_array_and_azp(realm, auth_server, make_clients, write_token):
    """Another audience in the ID token makes aud an array; our client is in it and azp is ours."""
    server = auth_server()
    f = write_token(realm.obtain_tokens("alice", "pw")["id_token"])
    make_clients(server)(1, extra_args=["--auth-token-file", str(f)])
    expect(server).to_have_player_login(realm.user_id("alice"), name="alice")


def test_roles_and_groups_reach_lua(idp, auth_server, make_clients, write_token):
    idp.add_user("alice", "pw", email="a@example.com", groups=["admins"], roles=["moderator"])
    server = auth_server(claims=("email", "groups", "realm_access"))
    server.run_lua('''
        vb.on("chat", function(player, text)
            local c = player:get_login().claims
            local roles = {}
            for _, r in ipairs(c.realm_access.roles) do roles[#roles + 1] = r end
            player:send_message("groups=" .. table.concat(c.groups, ",") .. " roles=" .. table.concat(roles, ","))
        end)
    ''')
    f = write_token(idp.obtain_tokens("alice", "pw")["id_token"])
    (alice,) = make_clients(server)(1, extra_args=["--auth-token-file", str(f)])
    alice.chat("who am i")
    expect(alice).to_have_chat(regex=r"groups=/?admins roles=.*moderator")


def test_stored_refresh_token_skips_the_browser(realm, auth_server, browser_join, make_clients, tmp_path):
    server = auth_server()
    alice, _ = browser_join(realm, server, "alice", home="alice")
    expect(server).to_have_player_login(realm.user_id("alice"), name="alice")
    alice.close()
    expect(server).to_have_player_count(0)
    asked = len(realm.requests("auth"))
    # The same install, a day later: the saved refresh token is enough, nothing opens a browser.
    (again,) = make_clients(server)(1, home="alice")
    expect(server).to_have_player_login(realm.user_id("alice"), name="alice")
    assert len(realm.requests("auth")) == asked
    assert realm.grants() == ["authorization_code", "refresh_token"]


def test_a_refresh_token_the_idp_no_longer_honours_falls_back_to_the_browser(realm, auth_server, browser_join,
                                                                            make_clients):
    server = auth_server()
    alice, _ = browser_join(realm, server, "alice", home="alice")
    alice.close()
    realm.admin_logout("alice")            # the saved sign-in is dead now
    second, player = browser_join(realm, server, "alice", home="alice", tag="alice2")
    expect(server).to_have_player_login(realm.user_id("alice"), name="alice")
    assert realm.grants() == ["authorization_code", "refresh_token", "authorization_code"]


@pytest.mark.realm(redirect_pattern="https://example.invalid/*")
def test_redirect_uri_mismatch(realm, auth_server, browser_join):
    server = auth_server()
    _, player = browser_join(realm, server, "alice", outcome="stuck")
    player.join(raises=False)
    assert player.refused is not None and "redirect_uri" in str(player.refused)
    assert realm.requests("auth")[0]["status"] == 400 and realm.requests("login") == []
    expect(server).to_have_player_count(0)


@pytest.mark.realm(audience_mapper=True)
def test_an_access_token_is_not_a_login(realm, auth_server, make_clients, write_token):
    """Q1 (docs/auth-keycloak-testing.md 6): with an audience mapper for our own client, the access
    token carries our client in aud, azp is ours and the realm key signed it. Only rule 1b stops it."""
    server = auth_server()
    tokens = realm.obtain_tokens("alice", "pw")
    bad = write_token(tokens["access_token"], "access.txt")
    with pytest.raises(ProcessDied):
        make_clients(server)(1, extra_args=["--auth-token-file", str(bad)])
    expect(server).to_have_player_count(0)
    expect(server).to_have_logged(r"sign-in rejected: token_type: typ claim is 'Bearer'")
    good = write_token(tokens["id_token"], "id.txt")
    make_clients(server)(1, extra_args=["--auth-token-file", str(good)])
    expect(server).to_have_player_login(realm.user_id("alice"), name="alice")


# =========================================================================================================
# K4.4 session and revocation scenarios (driven by advance_reauth)
# =========================================================================================================
def test_silent_reauth_keeps_the_player(realm, auth_server, browser_join):
    server = auth_server(**FAST_REAUTH)
    alice, _ = browser_join(realm, server, "alice")
    for done in (0, 1):                                            # two rounds: the rotated token works too
        reauth_round(server, "alice", done)
    assert refresh_grants(realm) >= 2
    expect(server).to_have_player_count(1)
    expect(alice).not_.to_have_auth(reauth_prompt=True, timeout=1)


def assert_kicked_after_grace(server, client, name):
    """The client's silent refresh failed (its banner is up); once the grace period is used up the
    server fails closed."""
    server.advance_reauth(name, 1_000_000)                         # the request goes out
    expect(client).to_have_auth(reauth_prompt=True, timeout=30)    # the IdP refused: "sign in again"
    server.advance_reauth(name, 1_000_000)                         # ... and the grace period runs out
    expect(server).to_have_player_count(0, timeout=30)
    expect(server).to_have_logged(r"re-auth deadline passed for '%s'" % name)


def test_admin_logout_kicks_within_interval_plus_grace(realm, auth_server, browser_join):
    """Closes the 9.6 "not verified" item: a sign-out in the admin console ends the player's game
    after one re-auth interval plus the grace period (here fast-forwarded through the real code)."""
    server = auth_server(**FAST_REAUTH)
    erin, _ = browser_join(realm, server, "erin")
    realm.admin_logout("erin")
    assert_kicked_after_grace(server, erin, "erin")
    assert realm.grants().count("refresh_token") >= 1                # the client really asked the IdP first
    assert [e["status"] for e in realm.requests("token")][-1] == 400


def test_disabled_user_is_kicked(realm, auth_server, browser_join):
    server = auth_server(**FAST_REAUTH)
    frank, _ = browser_join(realm, server, "frank")
    realm.disable_user("frank")
    assert_kicked_after_grace(server, frank, "frank")


def test_group_removed_fires_login_changed_once(realm, auth_server, browser_join):
    realm.add_user("carl", "pw", groups=["staff"])                  # replaces the plain carl
    server = auth_server(claims=("email", "groups"), **FAST_REAUTH)
    server.run_lua('''
        vb.on("login_changed", function(player, login)
            -- Keycloak leaves the claim out entirely for a user in no group
            player:send_message("login_changed groups=" .. #(login.claims.groups or {}))
        end)
    ''')
    carl, _ = browser_join(realm, server, "carl")
    sub = realm.user_id("carl")
    expect(server).to_have_player_login(sub, name="carl")
    realm.set_groups("carl", [])
    reauth_round(server, "carl", 0)
    expect(carl).to_have_chat("login_changed groups=0")
    expect(server).to_have_player_login(sub, name="carl")           # the in-game name stays
    reauth_round(server, "carl", 1)                                 # nothing changed this time
    assert sum("login_changed" in line for line in carl.state()["chat"]) == 1


def test_idle_session_timeout(realm, auth_server, browser_join):
    realm.set_realm(sso_session_idle_timeout=2)
    server = auth_server(**FAST_REAUTH)
    dave, _ = browser_join(realm, server, "dave")
    time.sleep(3)                                                   # the IdP's session idles out (its clock is real)
    assert_kicked_after_grace(server, dave, "dave")
    assert realm.requests("token")[-1]["status"] == 400


@pytest.mark.slow
def test_key_rotation_mid_session(realm, auth_server, browser_join):
    """Needs a real minute: the server refetches the JWKS for an unknown kid at most once per 60 s,
    counted from its first load at startup."""
    server = auth_server(**FAST_REAUTH)
    alice, _ = browser_join(realm, server, "alice")
    old = realm.active_kid
    realm.rotate_keys()
    realm.retire_key(old)
    assert len(realm.requests("certs")) == 1                        # the startup load
    time.sleep(62)                                                  # past the unknown-kid refresh limit
    reauth_round(server, "alice", 0)
    assert len(realm.requests("certs")) == 2                        # refreshed exactly once
    expect(server).to_have_player_count(1)


def test_refresh_token_rotation(realm, auth_server, browser_join):
    realm.set_realm(revoke_refresh_token=True)
    server = auth_server(**FAST_REAUTH)
    alice, _ = browser_join(realm, server, "alice")
    for done in (0, 1, 2):          # each round presents the refresh token the previous round returned
        reauth_round(server, "alice", done)
    assert [e["status"] for e in realm.requests("token")] == [200, 200, 200, 200]
    expect(server).to_have_player_count(1)
    if isinstance(realm, MockKeycloak):  # the first refresh token is dead by now: it cannot be replayed
        used = [rt for rt, e in realm.refresh.items() if e["used"]]
        assert used, "no refresh token was rotated"
        body = urllib.parse.urlencode({"grant_type": "refresh_token", "client_id": realm.client_id,
                                       "refresh_token": used[0]}).encode()
        with pytest.raises(urllib.error.HTTPError) as e:
            urllib.request.urlopen(urllib.request.Request(
                realm.local_url + "/protocol/openid-connect/token", data=body), timeout=10)
        assert json.loads(e.value.read())["error_description"] == "Maximum allowed refresh token reuse exceeded"


# =========================================================================================================
# K4.5 IdP failure scenarios (mock only: they need fault injection)
# =========================================================================================================
@pytest.mark.mock_only
def test_idp_down_at_server_start(idp, auth_server, make_clients, write_token):
    idp.add_user("alice", "pw")
    f = write_token(idp.obtain_tokens("alice", "pw")["id_token"])
    idp.faults.drop("discovery")
    server = auth_server()
    with pytest.raises(ProcessDied):                                # fails closed while down
        make_clients(server)(1, extra_args=["--auth-token-file", str(f)])
    expect(server).to_have_player_count(0)
    expect(server).to_have_logged("key fetch failed")
    idp.faults.clear()                                              # Keycloak is back: no server restart needed
    expect(server).to_have_logged("JWKS loaded", timeout=90)
    make_clients(server)(1, extra_args=["--auth-token-file", str(f)])
    expect(server).to_have_player_login(idp.user_id("alice"), name="alice")


@pytest.mark.mock_only
def test_jwks_5xx_then_recovers(idp, auth_server, make_clients, write_token):
    idp.add_user("alice", "pw")
    f = write_token(idp.obtain_tokens("alice", "pw")["id_token"])
    idp.faults.status("certs", 503, times=1)
    server = auth_server()
    expect(server).to_have_logged("JWKS loaded", timeout=60)       # after one retry
    log = idp.requests("certs")
    assert [e["status"] for e in log] == [503, 200]                 # one fetch at a time, no hammering
    assert log[1]["t"] - log[0]["t"] >= 9, "the retry must back off (10 s)"
    make_clients(server)(1, extra_args=["--auth-token-file", str(f)])
    expect(server).to_have_player_login(idp.user_id("alice"), name="alice")


@pytest.mark.mock_only
def test_a_slow_token_endpoint_still_joins(realm, auth_server, browser_join):
    realm.faults.latency("token", 5)
    server = auth_server()
    alice, _ = browser_join(realm, server, "alice")
    expect(server).to_have_player_login(realm.user_id("alice"), name="alice")
    assert "latency=5s" in realm.requests("token")[0]["fault"]


@pytest.mark.mock_only
def test_idp_down_during_reauth_but_back_before_the_grace_period_ends(realm, auth_server, browser_join):
    server = auth_server(**FAST_REAUTH)
    alice, _ = browser_join(realm, server, "alice")
    realm.faults.drop("token")
    server.advance_reauth("alice", 1_000_000)
    eventually(lambda: len([e for e in realm.requests("token") if e["fault"]]) >= 2,
               "the client to keep retrying while the IdP is down", timeout=30)
    expect(alice).not_.to_have_auth(reauth_prompt=True, timeout=1)    # a few quiet misses, no scary banner
    expect(server).to_have_player_count(1)
    realm.faults.clear()                                              # recovered inside the grace period
    expect(server).to_have_logged("re-auth ok for 'alice'", timeout=30)
    expect(server).to_have_player_count(1)
    expect(server).not_.to_have_logged("re-auth deadline passed", timeout=1)


@pytest.mark.mock_only
def test_idp_down_longer_than_the_grace_period_fails_closed(realm, auth_server, browser_join):
    server = auth_server(**FAST_REAUTH)
    alice, _ = browser_join(realm, server, "alice")
    realm.faults.drop("token")
    server.advance_reauth("alice", 1_000_000)
    eventually(lambda: any(e["fault"] for e in realm.requests("token")), "the first failed refresh", timeout=30)
    server.advance_reauth("alice", 1_000_000)                        # the grace period is over
    expect(server).to_have_player_count(0, timeout=30)
    expect(server).to_have_logged("re-auth deadline passed for 'alice'")


@pytest.mark.mock_only
@pytest.mark.parametrize("body", [pytest.param(b"{", id="malformed"), pytest.param(b"x" * (2 << 20), id="oversized")])
def test_malformed_and_oversized_jwks_fail_closed(idp, auth_server, make_clients, write_token, body):
    idp.add_user("alice", "pw")
    f = write_token(idp.obtain_tokens("alice", "pw")["id_token"])
    idp.faults.body("certs", body)
    server = auth_server()
    expect(server).to_have_logged("key fetch failed", timeout=30)
    with pytest.raises(ProcessDied):
        make_clients(server)(1, extra_args=["--auth-token-file", str(f)])
    expect(server).to_have_player_count(0)
    assert any(e["fault"] for e in idp.requests("certs"))
    assert server.alive                                              # no crash (ASan in CI would say so)


@pytest.mark.mock_only
def test_discovery_issuer_mismatch_refuses_every_join(idp, auth_server, make_clients, write_token):
    idp.add_user("alice", "pw")
    f = write_token(idp.obtain_tokens("alice", "pw")["id_token"])
    idp.faults.discovery_issuer("https://evil.example/realms/x")
    server = auth_server()
    with pytest.raises(ProcessDied):
        make_clients(server)(1, extra_args=["--auth-token-file", str(f)])
    expect(server).to_have_logged("discovery issuer does not match")
    expect(server).to_have_player_count(0)
    assert idp.requests("certs") == []                               # never fetches keys from a document it distrusts


@pytest.mark.mock_only
def test_sign_in_rate_limit(idp, auth_server, make_clients, write_token):
    server = auth_server()
    expect(server).to_have_logged("JWKS loaded", timeout=30)
    keys_fetched = len(idp.requests("certs"))
    junk = write_token("not.a.jwt", "junk.txt")
    for _ in range(31):
        with pytest.raises(ProcessDied):
            make_clients(server)(1, extra_args=["--auth-token-file", str(junk)])
    expect(server).to_have_logged("too many sign-in attempts", count=1)
    assert len(idp.requests("certs")) == keys_fetched                # refused before any IdP or JWKS work
    expect(server).to_have_player_count(0)


# =========================================================================================================
# K4.6 singleplayer
# =========================================================================================================
SP_HOOK = '''
vb.on("chat", function(player, text)
    local l = player:get_login()
    player:send_message("login=" .. (l and l.name or "nil"))
end)
'''


def _singleplayer_pack(idp, tmp_path):
    init = (REPO / "content" / "base" / "init.lua").read_text() + SP_HOOK
    return make_pack(REPO, tmp_path / "sp", {"auth.lua": auth_lua(idp), "init.lua": init})


def test_singleplayer_runs_the_same_real_sign_in(realm, make_clients, tmp_path, monkeypatch):
    pack = _singleplayer_pack(realm, tmp_path)
    url_file = tmp_path / "auth-url.txt"
    monkeypatch.setenv("VB_AUTH_URL_FILE", str(url_file))
    player = BrowserPlayer(realm, url_file, "alice", "pw").start()
    (alice,) = make_clients(None)(1, singleplayer=str(pack), home="sp")
    player.join()
    alice.chat("hello")
    expect(alice).to_have_chat("login=alice")
    assert realm.grants() == ["authorization_code"]


def test_insecure_skip_auth_gives_no_login(realm, make_clients, tmp_path):
    pack = _singleplayer_pack(realm, tmp_path)
    (solo,) = make_clients(None)(1, singleplayer=str(pack), home="sp2", extra_args=["--insecure-skip-auth"])
    solo.chat("hello")
    expect(solo).to_have_chat("login=nil")
    assert realm.request_log() == []                                 # the IdP was never contacted
