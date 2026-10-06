# Testing authentication against a mock Keycloak — phased plan

> Status: **planned (2026-10-06)**. Tracked as Phase 9.9 in `REMAINING_TASKS.md`.
> Companion to `architecture_spec/auth.md` (design, §10 testing strategy) and
> `docs/auth.md` (operator guide). Each task below is meant to be picked up on its own:
> it lists the files it touches, what "done" means, and the command that proves it.

## 1. Why

Phase 9 shipped with good coverage of the *engine's* rules, but against an IdP that is
"OIDC-shaped", not "Keycloak-shaped":

- `tests/e2e/vbtest/mock_idp.py` serves discovery, one RSA key, an auth endpoint that always
  succeeds, and a token endpoint (code + PKCE, refresh). It has no users, no sessions, no key
  rotation, no ES256, no Keycloak error bodies, no Keycloak claim shapes (`azp`, `typ`, `sid`,
  `realm_access.roles`, `aud` arrays, full-path groups), and no way to inject faults.
- `tests/unit/auth_*_test.cpp` (68 cases) use a fake `HttpFetcher` with hand-written JSON.
- Several items are written down as **not verified**: a real Keycloak token (9.3), admin
  logout kicking within interval + grace (9.6), the IdP denying or failing a browser sign-in,
  JWKS key rotation mid-session, IdP outages, the first-use trust prompt and stored refresh
  tokens end to end.

The plan: grow the mock into a **Keycloak emulator** that is faithful where the engine cares,
check that emulator against a **real Keycloak** so it can't drift, then use it to close every
gap above at the unit and e2e level.

Constraints carried over from the existing harness (`tests/e2e/README.md`):

- The e2e harness needs nothing but `pytest`. The emulator stays **stdlib-only**, so ES256
  signing is pure Python like the existing RS256 code.
- All key material under `tests/e2e/fixtures/` is **test-only** and labelled as such.
- Real Keycloak (docker) is **opt-in** (`--vb-idp=keycloak`) and never needed for the
  default run, so local runs and the normal CI job stay hermetic.
- No engine behaviour changes "for tests" unless they are compiled out of distribution
  builds (`VB_WITH_AUTOMATION` / `VB_DISTRIBUTION`, `docs/e2e-automation.md` §7).

## 2. Phase overview

| Phase | What | Size | Depends on |
|---|---|---|---|
| K0 | Baseline: mark and time the current auth suite, make it repeatable | S | — |
| K1 | `MockKeycloak`: users, sessions, Keycloak claims and errors, keys, fault injection | L | K0 |
| K2 | Self-tests for the emulator (no game binaries) | S | K1 |
| K3 | Unit tests (C++) on Keycloak-shaped fixtures | M | K1 (fixtures), K5.3 refreshes them |
| K4 | E2E scenarios against the emulator | L | K1, K2 |
| K5 | Conformance against real Keycloak (docker), opt-in | M/L | K1, K4 |
| K6 | CI wiring, docs, bookkeeping | S | K3, K4 (K5 for the nightly job) |

K3 and K4 can run in parallel once K1 has landed. K5 can start once K1 has the
`IdpBackend` interface (K1.1).

Command conventions used below (from `tests/e2e/README.md`):

```bash
cmake -S . -B build-e2e -DVB_WITH_AUTOMATION=ON -DVB_WITH_NET=ON -DVB_WITH_LUA=ON \
      -DVB_WITH_COMPRESSION=ON -DVB_WITH_REPLICATION=ON -DVB_WITH_AUTH=ON
cmake --build build-e2e
UNIT='build-e2e/vb_tests'                      # doctest binary
E2E='python3 -m pytest tests/e2e --vb-build-dir build-e2e'
```

---

## K0 — Baseline (S)

Goal: know what we have before changing it, and make the slow parts selectable.

- [ ] **K0.1 Register markers.** In `tests/e2e/conftest.py` add the `auth` marker (every test
      in `test_auth*.py`), `slow` (anything waiting on a real re-auth interval, ≥ 60 s) and
      `keycloak_real` (K5 only; skipped unless `--vb-idp=keycloak`).
      *Done when:* `$E2E -m "auth and not slow" --co` lists every current auth test except
      `test_revoked_login_is_kicked_after_the_grace_period`.
- [ ] **K0.2 Record timings.** Run `$E2E -m auth --durations=0` three times and note the
      numbers in this file (§7). That gives the budget K4 must stay inside.
- [ ] **K0.3 Fix the env leak.** `test_browser_flow_with_pkce_over_a_loopback_redirect`
      edits `os.environ` by hand. Switch to `monkeypatch.setenv("VB_AUTH_URL_FILE", ...)` so a
      failure can't leak into later tests.
      *Done when:* `$E2E -m auth` passes, and so does a run that puts the browser-flow test
      first: `python3 -m pytest --vb-build-dir build-e2e
      tests/e2e/test_auth.py::test_browser_flow_with_pkce_over_a_loopback_redirect
      tests/e2e/test_auth.py`.

## K1 — `MockKeycloak` emulator (L)

New module `tests/e2e/vbtest/mock_keycloak.py`. `mock_idp.py` stays as a thin compatibility
wrapper (`MockIdp = MockKeycloak` with the old defaults) so `test_auth.py` keeps passing
unchanged throughout K1.

- [ ] **K1.1 Backend interface.** Define an `IdpBackend` protocol in
      `tests/e2e/vbtest/idp.py` that both the emulator and the real-Keycloak adapter (K5)
      implement. Tests only use this interface:
      `issuer`, `client_id`, `add_user(username, password, *, email, groups, roles, enabled)`,
      `mint(...)` (emulator only; real backend raises `NotSupported` and tests that need it are
      marked `mock_only`), `admin_logout(username)`, `disable_user(username)`,
      `set_groups(username, groups)`, `rotate_keys()`, `browser_login(auth_url, username,
      password)` (returns the final redirect), `request_log()`.
      An `idp` fixture in `conftest.py` picks the backend from `--vb-idp=mock|keycloak`
      (default `mock`).
- [ ] **K1.2 Keycloak-shaped discovery.** Serve
      `/realms/<realm>/.well-known/openid-configuration` with the fields Keycloak 26 returns
      that a client might read: `issuer`, `authorization_endpoint`
      (`.../protocol/openid-connect/auth`), `token_endpoint`, `jwks_uri` (`.../certs`),
      `end_session_endpoint`, `introspection_endpoint`, `userinfo_endpoint`,
      `response_types_supported`, `id_token_signing_alg_values_supported`,
      `code_challenge_methods_supported: ["plain","S256"]`, `grant_types_supported`.
      Move the paths to Keycloak's real `/protocol/openid-connect/*` layout.
- [ ] **K1.3 Users and SSO sessions.** In-memory realm model: users (id = UUID `sub`,
      username, password, email, `email_verified`, enabled, groups, realm roles), SSO sessions
      (`sid`, user, created, last refresh) with realm settings `sso_session_idle_timeout`
      and `sso_session_max_lifespan` (Keycloak defaults 30 min / 10 h; tests can shrink them).
      Refresh tokens are bound to a session; a dead or logged-out session makes the refresh
      grant fail exactly like Keycloak (K1.5).
- [ ] **K1.4 Keycloak ID-token claims.** Tokens carry `iss`, `sub`, `aud` (string by default,
      `["vb-e2e","account"]` when the realm is configured with an audience mapper), `azp`,
      `typ: "ID"`, `exp`, `iat`, `auth_time`, `sid`, `nonce` (code flow only — refresh-derived
      tokens omit it, as Keycloak does), `at_hash`, `preferred_username`, `email`,
      `email_verified`, `name`, `groups` (configurable: names, or full paths like `/admins`),
      `realm_access.roles`. Lifetimes follow realm settings (default access/ID token 300 s).
      `mint(...)` keeps every override the bad-token tests rely on (`iat`, `exp`, `aud`,
      `iss`, `alg`, `kid`, `tamper`) and adds `azp`, `typ`, `drop=[claims]`.
- [ ] **K1.5 Keycloak error bodies.** Return what Keycloak returns, so the client's error
      mapping is tested against real shapes:
      `400 {"error":"invalid_grant","error_description":"Code not valid"}` (reused or unknown
      code), `"Session not active"` (logged out), `"Token is not active"` (expired refresh),
      `"Invalid refresh token"`, `"PKCE verification failed: Invalid code verifier"`,
      `401 {"error":"unauthorized_client"}` (wrong client id), user disabled
      (`"Account disabled"` on the login page, `invalid_grant` on refresh).
      The auth endpoint redirects with `error=access_denied` / `error=login_required` when the
      test says the user cancels, and rejects a `redirect_uri` that does not match the
      client's pattern (`http://127.0.0.1/*`) with Keycloak's "Invalid parameter:
      redirect_uri" page (HTTP 400, no redirect).
- [ ] **K1.6 Optional real login form.** By default `/auth` logs `next_user` in and redirects
      straight away (current behaviour, keeps tests fast). With `interactive=True` it serves a
      minimal Keycloak-style form (`<form id="kc-form-login" action="...login-actions/
      authenticate?...">`) that needs a `username`/`password` POST and a session cookie. Tests
      play the browser through `IdpBackend.browser_login`, which is the same code path K5 uses
      against real Keycloak.
- [ ] **K1.7 Keys: RS256, ES256, rotation.** Add a test-only P-256 key to
      `tests/e2e/fixtures/idp_ec.json` and a second RSA key `idp_rsa_2.json` (generate once
      with a script `tests/e2e/fixtures/gen_keys.py`; header comment: TEST ONLY). Pure-Python
      ES256 signing (P-256 point arithmetic, RFC 6979 deterministic `k`, raw `r||s` JWS
      encoding). The realm has an active key and passive keys; `rotate_keys()` makes a new key
      active and keeps the old one in the JWKS (Keycloak behaviour), `retire_key(kid)` removes
      it. The realm's signing algorithm is a constructor argument (`alg="RS256"|"ES256"`).
- [ ] **K1.8 Fault injection.** A small `faults` API applied per endpoint:
      `latency(endpoint, seconds)`, `status(endpoint, code, times=n)`,
      `body(endpoint, raw_bytes)` (malformed JSON, oversized > 1 MiB), `drop(endpoint)`
      (close the socket), `discovery_issuer(other)` (issuer mismatch). Each fault is visible in
      the request log so a failure says which fault was active.
- [ ] **K1.9 Admin operations.** Python methods (and, for K5 parity, the same names on the
      real backend): `admin_logout(username)` ends all SSO sessions,
      `disable_user`/`enable_user`, `set_groups`, `set_roles`, `set_realm(**timeouts)`,
      `rotate_keys`. These replace the current `revoke_all`/`set_claims`, which stay as aliases
      until `test_auth.py` is migrated (K4.1).
- [ ] **K1.10 Request log and failure artefact.** Every request (method, path, form/query
      with secrets redacted to their first 4 chars, status, fault applied) goes into
      `request_log()`. On test failure the `idp` fixture writes it to
      `<artifacts>/idp.requests.jsonl`, and `vbtest.traceview` gets an "IdP" column.

*Done when (whole phase):* `test_auth.py` passes unchanged on top of `MockKeycloak`; the
emulator is still stdlib-only (`python3 -I -c "import vbtest.mock_keycloak"` with only
`pytest` installed).

## K2 — Emulator self-tests (S)

New `tests/e2e/test_mock_keycloak.py`, marked `auth`, needs no game binaries (skip the
`binaries` fixture), so it runs in seconds and catches emulator bugs before they look like
engine bugs.

- [ ] **K2.1 Crypto.** RS256 and ES256 tokens verify with independent pure-Python verify code
      (RSA `pow(s, e, n)` + PKCS#1 v1.5 padding check; ECDSA verify). RFC 6979 test vector for
      P-256/SHA-256 (RFC 6979 §A.2.5) gives the exact `r, s`.
- [ ] **K2.2 Protocol.** Discovery fields match K1.2; code flow with PKCE succeeds once and a
      reused code fails with "Code not valid"; wrong verifier fails; refresh after
      `admin_logout` fails with "Session not active"; refresh after idle timeout fails with
      "Token is not active"; `rotate_keys` keeps the old `kid` in the JWKS until retired.
- [ ] **K2.3 Faults.** Each fault in K1.8 produces the documented response and is recorded.

*Run:* `python3 -m pytest tests/e2e/test_mock_keycloak.py -q` (< 10 s).

## K3 — C++ unit tests on Keycloak-shaped fixtures (M)

The unit suite should see the same bytes a real Keycloak sends, not hand-written JSON.

- [ ] **K3.1 Golden fixtures.** `tests/unit/fixtures/keycloak/`: `discovery.json`,
      `jwks_rs256.json`, `jwks_es256.json`, `jwks_rotated.json`, `token_response.json`,
      `errors/*.json` (every body from K1.5), and a set of ID tokens with known claims plus a
      `fixtures.md` saying how they were made. First generated by
      `tests/e2e/vbtest/mock_keycloak.py --dump-fixtures <dir>` with a fixed clock; K5.3
      re-generates them from a real Keycloak and the diff is reviewed.
      Because tokens expire, unit tests pass a fixed `now` to `verify_id_token` (it already
      takes one) and the fixture records the `now` it is valid at.
- [ ] **K3.2 Fixture loader.** A `KeycloakFixtureFetcher : HttpFetcher` in
      `tests/unit/auth_keycloak_fixture.hpp` that maps Keycloak URLs to fixture files and can
      be told to fail, delay or swap a file mid-test.
- [ ] **K3.3 New cases** in `tests/unit/auth_keycloak_test.cpp` (add it to
      `tests/CMakeLists.txt`'s explicit source list):
  - accept: real-shaped RS256 token; ES256 realm; `aud: ["vb-e2e","account"]` with
    `azp: "vb-e2e"`; refresh-derived token without `nonce`.
  - reject: `azp` set to another client; `aud` array without our client; `typ: "Bearer"`
    access token offered as an ID token (decide: reject on `typ` if present — see §6 Q1);
    token signed by a retired key after rotation; issuer with a trailing slash.
  - claims: `realm_access.roles` and full-path `groups` reach the allowlisted claims intact
    when listed in `claims`, and are dropped when not; a token with ~300 groups stays under
    the 16 KiB `C2S_Auth` cap or is rejected cleanly (not truncated).
  - JWKS: a new `kid` after rotation triggers exactly one refresh; the 60 s rate limit holds
    across repeated unknown `kid`s; a JWKS with one usable and one unusable (`use: enc`,
    RSA-OAEP) key keeps the usable one.
  - client: each K1.5 error body maps to the right `SignInCoordinator` outcome
    ("Session not active" / "Token is not active" / "Invalid refresh token" ⇒ stored refresh
    token forgotten, user asked to sign in again; 5xx ⇒ retryable, token kept).
  - discovery: issuer mismatch, missing `jwks_uri`, `http://` non-loopback issuer ⇒ fail
    closed with a log line naming the cause.

*Run:* `$UNIT -tc="*keycloak*"` and the full `ctest --test-dir build-e2e -R vb_tests`.

## K4 — E2E scenarios against the emulator (L)

New `tests/e2e/test_auth_keycloak.py` (the existing `test_auth.py` stays as the smoke suite).
Every test uses the `IdpBackend` interface only, so K5 can run the same file against real
Keycloak; tests that need `mint()` or fault injection are marked `mock_only`.

- [ ] **K4.1 Migrate fixtures.** `idp` and `auth_server` move to `conftest.py`; `auth_lua()`
      gains `alg`, `claims=("email","groups","realm_access")`, `audience` params.
      `test_auth.py` switches to the new admin method names.
- [ ] **K4.2 Test-only re-auth floor.** The engine's 60 s minimum interval makes revocation
      tests slow (≥ 70 s each). Add a server flag `--auth-test-min-reauth-seconds <n>` that
      lowers the floor, **compiled only under `VB_WITH_AUTOMATION`** and refused under
      `VB_DISTRIBUTION`, mirroring `--auth-token-file`. Cover the "refused in distribution"
      side in `tests/unit/auth_config_test.cpp`. (Decision needed — §6 Q2. Without it, the
      revocation tests below are all `slow`.)
- [ ] **K4.3 Sign-in scenarios**

  | Test | Setup | Expect |
  |---|---|---|
  | `browser_login_with_password` | `interactive=True`, user `alice/pw` | joins as `alice`; IdP log shows form POST, code exchange with `code_verifier` |
  | `user_cancels_at_idp` | auth endpoint redirects `error=access_denied` | client back at the sign-in screen with a "cancelled" reason; server never sees `C2S_Auth`; player count 0 |
  | `disabled_user_cannot_sign_in` | `disable_user("bob")` before join | no join; reason is coarse ("not accepted"), server log names the cause |
  | `es256_realm` | realm `alg="ES256"` | joins; JWKS fetched once |
  | `audience_array_and_azp` | audience mapper on | joins |
  | `roles_and_groups_reach_lua` | user in `/admins`, role `moderator` | `player:get_login().claims.groups` / `.realm_access.roles` visible in a `run_lua` chat hook |
  | `stored_refresh_token_skips_browser` | sign in once (browser), disconnect, reconnect same client data dir | second join makes a `refresh_token` grant and no `/auth` request |
  | `first_use_trust_prompt` | fresh data dir, non-token-file client | nothing reaches the IdP until the trust prompt is accepted (assert empty request log, then accept through automation, then `/auth`) |
  | `redirect_uri_mismatch` | client registered with another pattern | IdP 400 page; client times out sign-in cleanly; no join |

- [ ] **K4.4 Session & revocation scenarios** (use K4.2; else marked `slow`)

  | Test | Action mid-session | Expect |
  |---|---|---|
  | `silent_reauth_keeps_player` | none | ≥ 2 re-auth rounds succeed, refresh grants in the log, player stays |
  | `admin_logout_kicks_within_interval_plus_grace` | `admin_logout("erin")` | kicked with "sign-in expired or revoked" within interval + grace (+ jitter) — closes the 9.6 "not verified" item |
  | `disabled_user_kicked` | `disable_user` | same as above |
  | `group_removed_fires_login_changed` | `set_groups("carl", [])` | `login_changed` fires once, name unchanged |
  | `idle_session_timeout` | realm `sso_session_idle_timeout` < interval | refresh fails "Token is not active"; banner shown on client; kicked after grace |
  | `key_rotation_mid_session` | `rotate_keys()` then `retire_key(old)` | next re-auth token has the new `kid`; server refreshes JWKS once; player stays |
  | `refresh_token_rotation` | realm "revoke refresh token" on | each refresh returns a new RT, client stores it; reusing the old one fails |

- [ ] **K4.5 IdP failure scenarios** (`mock_only`)

  | Test | Fault | Expect |
  |---|---|---|
  | `idp_down_at_server_start` | `drop(discovery)` then clear | joins fail closed while down; succeed after recovery without restarting the server |
  | `jwks_5xx_then_recovers` | `status(certs, 503, times=3)` | backoff visible in log; at most one in-flight fetch; join succeeds later |
  | `slow_token_endpoint` | `latency(token, 5)` | join still succeeds within `auth_timeout_seconds` |
  | `idp_down_during_reauth` | `drop(token)` for < grace | player keeps playing; recovered before grace ⇒ stays |
  | `idp_down_longer_than_grace` | `drop(token)` > grace | kicked (fail closed) |
  | `malformed_and_oversized_bodies` | `body(certs, b"{")`, `body(certs, 2 MiB)` | no crash (run under ASan in CI), joins fail closed, log says why |
  | `discovery_issuer_mismatch` | `discovery_issuer("https://evil/realms/x")` | server refuses all joins; log names the mismatch |
  | `sign_in_rate_limit` | 31 bad attempts from one IP in 60 s | 31st refused before any IdP/JWKS work |

- [ ] **K4.6 Singleplayer.** Integrated server with an auth pack and the emulator: browser
      flow joins; `--insecure-skip-auth` gives `get_login() == nil`.

*Run:* `$E2E -m "auth and not slow"` (target: under 3 min with K4.2) and `$E2E -m auth`
for the full set.

## K5 — Conformance against real Keycloak (M/L, opt-in)

Proves the emulator is honest and closes "not verified against a real Keycloak token".

- [ ] **K5.1 Realm export.** `tests/e2e/fixtures/keycloak/realm-e2e.json`: realm `e2e`,
      public client `vb-e2e` (standard flow only, PKCE S256 required, redirect
      `http://127.0.0.1/*`), group-membership mapper (`groups`, ID token on), audience mapper,
      users `alice`, `bob`, `carl`, `erin` with test-only passwords, short token lifetimes.
      Matches `docs/auth.md` §2 step by step — if the doc and the export disagree, fix the doc.
- [ ] **K5.2 Real backend.** `tests/e2e/vbtest/keycloak_real.py` implements `IdpBackend`:
      starts `quay.io/keycloak/keycloak:<pinned 26.x>` with
      `start-dev --import-realm --http-port 0`-equivalent port mapping (or uses
      `VB_KEYCLOAK_URL` if set), waits on `/health/ready`, does admin calls through the Admin
      REST API (`/admin/realms/e2e/users/{id}/logout`, `PUT .../users/{id}` enabled=false,
      group membership, `components` for key rotation), and `browser_login` by fetching the
      auth URL with a cookie jar, parsing `kc-form-login`'s `action`, POSTing credentials and
      following redirects to the client's loopback listener.
      Note: the engine allows `http://` issuers only on loopback, so the container is
      published on `127.0.0.1`.
- [ ] **K5.3 Contract/drift test.** `test_mock_keycloak_matches_real.py` (`keycloak_real`):
      for discovery, JWKS, a code-flow token, a refresh-flow token and each K1.5 error,
      compare the real response to the emulator's — same keys present, same types, same
      error codes/descriptions (values like timestamps and ids ignored). Also regenerates the
      K3.1 fixtures into a temp dir and fails with a diff if they changed.
- [ ] **K5.4 Run K4 against it.** `$E2E -m "auth and not mock_only" --vb-idp=keycloak`.
      Every non-`mock_only` test in `test_auth_keycloak.py` must pass unchanged.

*Run locally:* `docker` required;
`$E2E -m "keycloak_real or (auth and not mock_only)" --vb-idp=keycloak`.

## K6 — CI, docs, bookkeeping (S)

- [ ] **K6.1 Default e2e job** (`.github/workflows/build_linux.yml` `e2e`): already runs the
      whole suite; add `-m "not slow"` only if the K0.2 budget is blown, otherwise keep
      everything. Upload `idp.requests.jsonl` with the existing failure artefacts.
- [ ] **K6.2 Emulator self-tests everywhere.** K2 needs no binaries: add a cheap step to the
      lint workflow (`python3 -m pytest tests/e2e/test_mock_keycloak.py`).
- [ ] **K6.3 Nightly real-Keycloak job.** New workflow `auth_keycloak.yml`
      (`schedule` + `workflow_dispatch`, not on every PR): build the e2e config, start
      Keycloak as a job `services:` container, run K5.3 + K5.4. Failure opens nothing
      automatically; the run is the record.
- [ ] **K6.4 Docs.** `tests/e2e/README.md` "Authentication tests" section: the emulator, the
      markers, `--vb-idp`, how to run against docker Keycloak. `docs/auth.md` §7 points here.
      `architecture_spec/auth.md` §10 "Manual: real Keycloak" becomes "nightly K5 job;
      Firebase still manual".
- [ ] **K6.5 Bookkeeping.** Tick the closed "not verified" lines in `REMAINING_TASKS.md`
      9.3/9.6/9.8 and move the detail into `remaining_tasks/` per the file's convention.

## 6. Open questions (decide before the task that needs it)

1. **`typ` claim (K3.3).** Keycloak puts `typ: "ID"` in ID tokens and `typ: "Bearer"` in
   access tokens, both signed with the same key and `aud` can overlap. Reject a token whose
   `typ` is present and not `ID`? Recommended: yes for the `keycloak` preset only (generic
   OIDC IdPs don't set it). This is an engine change, so it gets its own commit with a
   verifier rule and a unit test.
2. **Test-only re-auth floor (K4.2).** Recommended: add it (automation builds only). The
   alternative is ~10 tests at ≥ 70 s each, which pushes the e2e job past its 75-minute
   timeout under ASan.
3. **Docker in CI (K6.3).** GitHub's Ubuntu runners have docker; the plan keeps it to a
   nightly job so a Keycloak image pull never blocks a PR.
4. **Back-channel logout** stays out of scope (`architecture_spec/auth.md` §13 Q4); if it
   lands later, the emulator gets a `backchannel_logout_uri` POST in K1.9 and K4.4 gets an
   "instant kick" row.

## 7. Baseline timings (filled by K0.2)

| Run | `-m "auth and not slow"` | `-m auth` |
|---|---|---|
| 1 | | |
| 2 | | |
| 3 | | |
