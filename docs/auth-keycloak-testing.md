# Testing authentication against a mock Keycloak — phased plan

> Status: **done (2026-10-06); decisions Q1–Q3 accepted 2026-10-06 (§6); what changed on the way is in §8**.
> Tracked as Phase 9.9 in `REMAINING_TASKS.md`. The checklists below say what was asked for; §8 says where
> reality differed, and the table in §2 says where each piece landed.
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

- [x] **K0.1 Register markers.** In `tests/e2e/conftest.py` add the `auth` marker (every test
      in `test_auth*.py`), `slow` (anything waiting on a real re-auth interval, ≥ 60 s) and
      `keycloak_real` (K5 only; skipped unless `--vb-idp=keycloak`).
      *Done when:* `$E2E -m "auth and not slow" --co` lists every current auth test except
      `test_revoked_login_is_kicked_after_the_grace_period`.
- [x] **K0.2 Record timings.** Run `$E2E -m auth --durations=0` three times and note the
      numbers in this file (§7). That gives the budget K4 must stay inside.
- [x] **K0.3 Fix the env leak.** `test_browser_flow_with_pkce_over_a_loopback_redirect`
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

- [x] **K1.1 Backend interface.** Define an `IdpBackend` protocol in
      `tests/e2e/vbtest/idp.py` that both the emulator and the real-Keycloak adapter (K5)
      implement. Tests only use this interface:
      `issuer`, `client_id`, `add_user(username, password, *, email, groups, roles, enabled)`,
      `mint(...)` (emulator only; real backend raises `NotSupported` and tests that need it are
      marked `mock_only`), `admin_logout(username)`, `disable_user(username)`,
      `set_groups(username, groups)`, `rotate_keys()`, `browser_login(auth_url, username,
      password)` (returns the final redirect), `request_log()`.
      An `idp` fixture in `conftest.py` picks the backend from `--vb-idp=mock|keycloak`
      (default `mock`).
- [x] **K1.2 Keycloak-shaped discovery.** Serve
      `/realms/<realm>/.well-known/openid-configuration` with the fields Keycloak 26 returns
      that a client might read: `issuer`, `authorization_endpoint`
      (`.../protocol/openid-connect/auth`), `token_endpoint`, `jwks_uri` (`.../certs`),
      `end_session_endpoint`, `introspection_endpoint`, `userinfo_endpoint`,
      `response_types_supported`, `id_token_signing_alg_values_supported`,
      `code_challenge_methods_supported: ["plain","S256"]`, `grant_types_supported`.
      Move the paths to Keycloak's real `/protocol/openid-connect/*` layout.
- [x] **K1.3 Users and SSO sessions.** In-memory realm model: users (id = UUID `sub`,
      username, password, email, `email_verified`, enabled, groups, realm roles), SSO sessions
      (`sid`, user, created, last refresh) with realm settings `sso_session_idle_timeout`
      and `sso_session_max_lifespan` (Keycloak defaults 30 min / 10 h; tests can shrink them).
      Refresh tokens are bound to a session; a dead or logged-out session makes the refresh
      grant fail exactly like Keycloak (K1.5).
- [x] **K1.4 Keycloak ID-token claims.** Tokens carry `iss`, `sub`, `aud` (string by default,
      `["vb-e2e","account"]` when the realm is configured with an audience mapper), `azp`,
      `typ: "ID"`, `exp`, `iat`, `auth_time`, `sid`, `nonce` (code flow only — refresh-derived
      tokens omit it, as Keycloak does), `at_hash`, `preferred_username`, `email`,
      `email_verified`, `name`, `groups` (configurable: names, or full paths like `/admins`),
      `realm_access.roles`. Lifetimes follow realm settings (default access/ID token 300 s).
      `mint(...)` keeps every override the bad-token tests rely on (`iat`, `exp`, `aud`,
      `iss`, `alg`, `kid`, `tamper`) and adds `azp`, `typ`, `drop=[claims]`.
- [x] **K1.5 Keycloak error bodies.** Return what Keycloak returns, so the client's error
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
- [x] **K1.6 Optional real login form.** By default `/auth` logs `next_user` in and redirects
      straight away (current behaviour, keeps tests fast). With `interactive=True` it serves a
      minimal Keycloak-style form (`<form id="kc-form-login" action="...login-actions/
      authenticate?...">`) that needs a `username`/`password` POST and a session cookie. Tests
      play the browser through `IdpBackend.browser_login`, which is the same code path K5 uses
      against real Keycloak.
- [x] **K1.7 Keys: RS256, ES256, rotation.** Add a test-only P-256 key to
      `tests/e2e/fixtures/idp_ec.json` and a second RSA key `idp_rsa_2.json` (generate once
      with a script `tests/e2e/fixtures/gen_keys.py`; header comment: TEST ONLY). Pure-Python
      ES256 signing (P-256 point arithmetic, RFC 6979 deterministic `k`, raw `r||s` JWS
      encoding). The realm has an active key and passive keys; `rotate_keys()` makes a new key
      active and keeps the old one in the JWKS (Keycloak behaviour), `retire_key(kid)` removes
      it. The realm's signing algorithm is a constructor argument (`alg="RS256"|"ES256"`).
- [x] **K1.8 Fault injection.** A small `faults` API applied per endpoint:
      `latency(endpoint, seconds)`, `status(endpoint, code, times=n)`,
      `body(endpoint, raw_bytes)` (malformed JSON, oversized > 1 MiB), `drop(endpoint)`
      (close the socket), `discovery_issuer(other)` (issuer mismatch). Each fault is visible in
      the request log so a failure says which fault was active.
- [x] **K1.9 Admin operations.** Python methods (and, for K5 parity, the same names on the
      real backend): `admin_logout(username)` ends all SSO sessions,
      `disable_user`/`enable_user`, `set_groups`, `set_roles`, `set_realm(**timeouts)`,
      `rotate_keys`. These replace the current `revoke_all`/`set_claims`, which stay as aliases
      until `test_auth.py` is migrated (K4.1).
- [x] **K1.10 Request log and failure artefact.** Every request (method, path, form/query
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

- [x] **K2.1 Crypto.** RS256 and ES256 tokens verify with independent pure-Python verify code
      (RSA `pow(s, e, n)` + PKCS#1 v1.5 padding check; ECDSA verify). RFC 6979 test vector for
      P-256/SHA-256 (RFC 6979 §A.2.5) gives the exact `r, s`.
- [x] **K2.2 Protocol.** Discovery fields match K1.2; code flow with PKCE succeeds once and a
      reused code fails with "Code not valid"; wrong verifier fails; refresh after
      `admin_logout` fails with "Session not active"; refresh after idle timeout fails with
      "Token is not active"; `rotate_keys` keeps the old `kid` in the JWKS until retired.
- [x] **K2.3 Faults.** Each fault in K1.8 produces the documented response and is recorded.

*Run:* `python3 -m pytest tests/e2e/test_mock_keycloak.py -q` (< 10 s).

## K3 — C++ unit tests on Keycloak-shaped fixtures (M)

The unit suite should see the same bytes a real Keycloak sends, not hand-written JSON.

- [x] **K3.1 Golden fixtures.** `tests/unit/fixtures/keycloak/`: `discovery.json`,
      `jwks_rs256.json`, `jwks_es256.json`, `jwks_rotated.json`, `token_response.json`,
      `errors/*.json` (every body from K1.5), and a set of ID tokens with known claims plus a
      `fixtures.md` saying how they were made. First generated by
      `tests/e2e/vbtest/mock_keycloak.py --dump-fixtures <dir>` with a fixed clock; K5.3
      re-generates them from a real Keycloak and the diff is reviewed.
      Because tokens expire, unit tests pass a fixed `now` to `verify_id_token` (it already
      takes one) and the fixture records the `now` it is valid at.
- [x] **K3.2 Fixture loader.** A `KeycloakFixtureFetcher : HttpFetcher` in
      `tests/unit/auth_keycloak_fixture.hpp` that maps Keycloak URLs to fixture files and can
      be told to fail, delay or swap a file mid-test.
- [x] **K3.3 New cases** in `tests/unit/auth_keycloak_test.cpp` (add it to
      `tests/CMakeLists.txt`'s explicit source list):
  - accept: real-shaped RS256 token; ES256 realm; `aud: ["vb-e2e","account"]` with
    `azp: "vb-e2e"`; refresh-derived token without `nonce`.
  - reject: `azp` set to another client; `aud` array without our client; `typ: "Bearer"`
    access token offered as an ID token (rule from K3.4);
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

- [x] **K3.4 Token-type rule (engine; decided §6 Q1).** In `src/auth/verifier.cpp` add
      rule 1b: reject a JWS header `typ` of `at+jwt` (any case) for every preset, and for the
      `keycloak` preset require the payload claim `typ == "ID"` (missing ⇒ reject). New
      `VerifyError` value, exact cause in the server log, coarse reason to the player. Update
      `architecture_spec/auth.md` §5.3 and `docs/auth.md` §4. Own commit, with accept/reject
      cases built from the K3.1 fixtures (`typ: "Bearer"`, `"Logout"`, missing `typ`,
      `at+jwt` header on an otherwise valid token).

*Run:* `$UNIT -tc="*keycloak*"` and the full `ctest --test-dir build-e2e -R vb_tests`.

## K4 — E2E scenarios against the emulator (L)

New `tests/e2e/test_auth_keycloak.py` (the existing `test_auth.py` stays as the smoke suite).
Every test uses the `IdpBackend` interface only, so K5 can run the same file against real
Keycloak; tests that need `mint()` or fault injection are marked `mock_only`.

- [x] **K4.1 Migrate fixtures.** `idp` and `auth_server` move to `conftest.py`; `auth_lua()`
      gains `alg`, `claims=("email","groups","realm_access")`, `audience` params.
      `test_auth.py` switches to the new admin method names.
- [x] **K4.2 Fast-forward re-auth (automation command).** Add a server automation command
      `advance_reauth {seconds, player?}` that subtracts `seconds` from the player's re-auth
      timer and, while a request is outstanding, from its grace countdown. The next tick then
      runs the normal `system_reauth` path. The command lives in
      `src/server/automation_endpoint.hpp`, so it is compiled out of distribution builds like
      every other automation command. The `reauth_interval_seconds` floor in `auth.lua`
      (60 s) is not touched. Keep exactly one wall-clock test (the existing
      `test_revoked_login_is_kicked_after_the_grace_period`, marked `slow`) so the real timer
      and jitter are still covered. See §6 Q2 for why this beats lowering the floor.
- [x] **K4.3 Sign-in scenarios**

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

- [x] **K4.4 Session & revocation scenarios** (driven by `advance_reauth`, K4.2)

  | Test | Action mid-session | Expect |
  |---|---|---|
  | `silent_reauth_keeps_player` | none | ≥ 2 re-auth rounds succeed, refresh grants in the log, player stays |
  | `admin_logout_kicks_within_interval_plus_grace` | `admin_logout("erin")` | kicked with "sign-in expired or revoked" within interval + grace (+ jitter) — closes the 9.6 "not verified" item |
  | `disabled_user_kicked` | `disable_user` | same as above |
  | `group_removed_fires_login_changed` | `set_groups("carl", [])` | `login_changed` fires once, name unchanged |
  | `idle_session_timeout` | realm `sso_session_idle_timeout` < interval | refresh fails "Token is not active"; banner shown on client; kicked after grace |
  | `key_rotation_mid_session` | `rotate_keys()` then `retire_key(old)` | next re-auth token has the new `kid`; server refreshes JWKS once; player stays |
  | `refresh_token_rotation` | realm "revoke refresh token" on | each refresh returns a new RT, client stores it; reusing the old one fails |

- [x] **K4.5 IdP failure scenarios** (`mock_only`)

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

- [x] **K4.6 Singleplayer.** Integrated server with an auth pack and the emulator: browser
      flow joins; `--insecure-skip-auth` gives `get_login() == nil`.

*Run:* `$E2E -m "auth and not slow"` (target: under 3 min, using `advance_reauth`) and `$E2E -m auth`
for the full set.

## K5 — Conformance against real Keycloak (M/L, opt-in)

Proves the emulator is honest and closes "not verified against a real Keycloak token".

- [x] **K5.1 Realm export.** `tests/e2e/fixtures/keycloak/realm-e2e.json`: realm `e2e`,
      public client `vb-e2e` (standard flow only, PKCE S256 required, redirect
      `http://127.0.0.1/*`), group-membership mapper (`groups`, ID token on), audience mapper,
      users `alice`, `bob`, `carl`, `erin` with test-only passwords, short token lifetimes.
      Matches `docs/auth.md` §2 step by step — if the doc and the export disagree, fix the doc.
- [x] **K5.2 Real backend.** `tests/e2e/vbtest/keycloak_real.py` implements `IdpBackend`:
      starts `quay.io/keycloak/keycloak:<pinned 26.x>` with
      `start-dev --import-realm --http-port 0`-equivalent port mapping (or uses
      `VB_KEYCLOAK_URL` if set), waits on `/health/ready`, does admin calls through the Admin
      REST API (`/admin/realms/e2e/users/{id}/logout`, `PUT .../users/{id}` enabled=false,
      group membership, `components` for key rotation), and `browser_login` by fetching the
      auth URL with a cookie jar, parsing `kc-form-login`'s `action`, POSTing credentials and
      following redirects to the client's loopback listener.
      Note: the engine allows `http://` issuers only on loopback, so the container is
      published on `127.0.0.1`.
- [x] **K5.3 Contract/drift test.** `test_mock_keycloak_matches_real.py` (`keycloak_real`):
      for discovery, JWKS, a code-flow token, a refresh-flow token and each K1.5 error,
      compare the real response to the emulator's — same keys present, same types, same
      error codes/descriptions (values like timestamps and ids ignored). Also regenerates the
      K3.1 fixtures into a temp dir and fails with a diff if they changed.
- [x] **K5.4 Run K4 against it.** `$E2E -m "auth and not mock_only" --vb-idp=keycloak`.
      Every non-`mock_only` test in `test_auth_keycloak.py` must pass unchanged.

*Run locally:* `docker` required;
`$E2E -m "keycloak_real or (auth and not mock_only)" --vb-idp=keycloak`.

## K6 — CI, docs, bookkeeping (S)

- [x] **K6.1 Default e2e job** (`.github/workflows/build_linux.yml` `e2e`): already runs the
      whole suite; add `-m "not slow"` only if the K0.2 budget is blown, otherwise keep
      everything. Upload `idp.requests.jsonl` with the existing failure artefacts.
- [x] **K6.2 Emulator self-tests everywhere.** K2 needs no binaries: add a cheap step to the
      lint workflow (`python3 -m pytest tests/e2e/test_mock_keycloak.py`).
- [x] **K6.3 Real-Keycloak workflow.** New workflow `auth_keycloak.yml` that builds the e2e
      config, starts the **pinned** Keycloak image as a job `services:` container and runs
      K5.3 + K5.4. Triggers: pull requests that touch `src/auth/**`, `inc/vb/auth/**`,
      `src/net/session.cpp`, `tests/e2e/vbtest/*idp*`/`*keycloak*`, `tests/e2e/fixtures/**`
      or the workflow itself; `workflow_dispatch`; and a weekly canary that runs the same
      tests against Keycloak's newest release tag instead of the pinned one. The PR job
      starts non-required and becomes a required check after ~2 weeks without a flake.
      The canary opens or updates one tracking issue when it fails. See §6 Q3.
- [x] **K6.4 Docs.** `tests/e2e/README.md` "Authentication tests" section: the emulator, the
      markers, `--vb-idp`, how to run against docker Keycloak. `docs/auth.md` §7 points here.
      `architecture_spec/auth.md` §10 "Manual: real Keycloak" becomes "K5 workflow (auth PRs +
      weekly canary); Firebase still manual".
- [x] **K6.5 Bookkeeping.** Tick the closed "not verified" lines in `REMAINING_TASKS.md`
      9.3/9.6/9.8 and move the detail into `remaining_tasks/` per the file's convention.

## 6. Decisions and their trade-offs

**All three recommendations were accepted on 2026-10-06** (Q1: B + D; Q2: C plus one slow
wall-clock test; Q3: C + D plus `workflow_dispatch`). The options and their costs stay
below as the record of why.

### Q1. Check the `typ` claim? (engine change; blocks K3.3)

**The problem.** The verifier (`src/auth/verifier.cpp`) checks `alg`, `kid`, the signature,
`iss`, `aud`, `azp`, time and `nonce`, but not what *kind* of token it got. Keycloak signs
three kinds of JWT with the realm key: ID tokens (`typ: "ID"`), access tokens
(`typ: "Bearer"`) and logout tokens (`typ: "Logout"`); refresh tokens are `typ: "Refresh"` but
HMAC-signed, so the `alg` allowlist already rejects them. By default a Keycloak access token
has `aud: "account"` and fails our audience check, which is why this hasn't mattered. But
`docs/auth.md` §2 and the K5.1 realm both suggest an **audience mapper**, and once a mapper
adds `vb-e2e` to `aud`, an access token passes every rule we have: same issuer, same key,
`azp` equals our client, fresh `iat`.

Why that matters: access tokens are shared more widely than ID tokens. A pack's own web
service, a bot or a resource server may receive the player's access token. Anything that
holds one could present it as a login during its lifetime (5 min by default, up to
`max_token_age_seconds`). The server nonce does not stop this on the refresh path (rule 6
can't apply there), and some Keycloak versions also copy `nonce` into access tokens.

| Option | Security | Compatibility risk | Cost |
|---|---|---|---|
| A. Do nothing; rely on `aud` | Gap opens whenever an operator adds an audience mapper, which our own docs recommend | None | None |
| B. `keycloak` preset: require `typ == "ID"` (reject if missing) | Closes the gap for Keycloak | Breaks if a Keycloak version stops sending `typ`. It has sent it for many years, and the K5 drift test would catch a change before release | ~15 lines + unit cases |
| C. `keycloak` preset: reject `typ` only if present and not `ID` | Same as B for real Keycloak tokens. Weaker in theory: a token without `typ` passes | None | Same as B |
| D. All presets: also reject JWS header `typ: "at+jwt"` (RFC 9068 access tokens) | Covers IdPs that follow RFC 9068 and mark access tokens this way | Very low: no IdP issues ID tokens with that header | ~5 lines |
| E. Change the docs: tell operators not to add the audience mapper | Partial: depends on operators reading the docs | None | Docs only |

Who could break: real players can't, because the engine client only ever sends the
`id_token` from the token response. Only `--auth-token-file` users who paste an access
token by mistake would see a change, and for them a clear rejection is the right outcome.

**Decided (2026-10-06): B + D.** Strict `typ == "ID"` for the `keycloak` preset, and reject
`at+jwt` headers for every preset. Log the exact reason on the server; the player still sees
the coarse "not accepted by this server". It lands as its own commit, rule 1b in
`architecture_spec/auth.md` §5.3, with unit cases from K3.1's fixtures. If you'd rather not
change the engine during a testing phase, fall back to **C now, B later**. Avoid A: the
K4.3 `audience_array_and_azp` test would then show the gap without catching it.

### Q2. How to test re-auth without waiting for real time (blocks K4.2/K4.4)

**The problem.** `auth.lua` requires `reauth_interval_seconds ≥ 60` and
`reauth_grace_seconds ≥ 10`. A revocation test therefore waits interval ± 10% jitter +
grace, about 70–80 s; a "silent re-auth works twice" test waits ~130 s. K4.4/K4.5 add about
nine such tests, so roughly **12–15 min**. The whole e2e suite is **one serial CTest entry
with a 900 s timeout** (`tests/CMakeLists.txt:190`) that already runs every other e2e test
under ASan. The extra time alone uses most of that budget.

Useful fact from the code: re-auth timers are counted in **tick time**, not wall-clock time.
`system_reauth(dt)` (`src/net/session.cpp:1737`) subtracts `dt` from `timer` and from
`remaining`. Only the stale-`iat` check uses wall-clock time (`host_.unix_time()`).

| Option | Suite time | How real the test is | Production risk | Flakiness | Cost |
|---|---|---|---|---|---|
| A. Keep 60 s; mark the tests `slow` and run them elsewhere (separate CTest entry, nightly) | Main suite unchanged; +12–15 min in the slow job | Full: real timers, real jitter | None | Low (long margins) | A second CTest entry and CI job. Revocation bugs are found a day late |
| B. Test-only flag that lowers the floor (e.g. interval 3 s, grace 2 s) | ~5–10 s per test | Real timers, but at a scale where ASan slowness (verification, refresh, a tick stall) is a large share of the interval | Adds a branch to operator-facing config validation, compiled out of distribution builds. One more `#ifdef` path to keep correct | **High**: ±10% of 3 s is 0.3 s; a slow JWKS fetch or an ASan pause can miss the window | Small: flag + validation + one unit test |
| C. Automation command `advance_reauth {seconds}` that moves the tick-time timers forward | ~2–5 s per test | Same `system_reauth` code path; only "time passing" is simulated | None for config. Lives in `automation_endpoint.hpp` with the other automation commands that distribution builds already exclude | **Low**: deterministic, no race against a short timer | Small: one command + docs row in `docs/automation-protocol.md` |
| D. Run e2e tests in parallel (pytest-xdist) | Wall time ÷ cores | Full | None | Medium: more processes under ASan on a 4-core runner, UDP port and CPU contention | Adds the harness's first dependency besides pytest; every test must be isolation-safe |
| E. Fake the whole server clock (unix time and ticks) | Fast | Lowest: tokens minted by the IdP at real time look stale/future to a skewed server unless the mock follows the same clock | None | Medium | Large: clock plumbing through the server and the mock |

C doesn't test that the timer fires on its own, or the jitter. One slow wall-clock test
covers that; it already exists (`test_revoked_login_is_kicked_after_the_grace_period`).

**Decided (2026-10-06): C, plus one slow wall-clock test.** This replaces the floor flag (B)
that the first draft of this plan recommended. B looks simpler, but it trades waiting for
races: a 3 s interval under ASan is exactly the kind of timing test that fails 1 run in 50.
It also changes validation of operator-facing config, which C leaves alone. Revisit A (a
separate `e2e-slow` entry) only if the suite approaches the 900 s budget anyway (K0.2
measures this).

### Q3. When to run tests against real Keycloak (blocks K6.3)

**The problem.** K5 needs docker, an image pull of several hundred MB, and 20–40 s for Keycloak to
start and import the realm. Image pulls can fail for reasons that have nothing to do with
the change (registry outage, rate limit). The point of K5 is to catch the mock drifting
from real Keycloak, and a pinned image only changes when someone bumps the pin.

That last point matters: with a pinned image, a nightly run re-tests code that only
changed through PRs. It finds drift no sooner than a PR job would, just later and further
from the change that caused it.

| Option | Finds an engine/mock regression | Finds a new Keycloak release breaking us | Blocks PRs on infra trouble | CI cost | Who acts on failure |
|---|---|---|---|---|---|
| A. Every PR, required | Immediately | No (pinned) | **Yes**: an image-pull failure blocks unrelated merges | Every PR pays ~3–5 min + pull | PR author |
| B. Nightly only (first draft) | Up to 24 h late, on main, author no longer in context | No (pinned) | No | Low | **Nobody by default.** A red nightly with no owner gets ignored |
| C. PRs that touch auth paths, non-required at first, required once stable | Immediately, on the PRs that can cause it | No (pinned) | Only auth PRs, and only once required | Low: most PRs don't touch auth | PR author |
| D. Weekly canary against Keycloak's **latest** release | No | **Yes**, before we bump the pin | No | Low | Tracking issue the canary opens |
| E. Manual only (`workflow_dispatch`) | Only if someone remembers | Only if someone remembers | No | Lowest | Whoever runs it |

Path filters have a known gap: a change outside the listed paths can still break auth
(protocol code in `src/protocol/handshake.cpp`, a shared HTTP change). The default e2e job
still runs the full mock suite on every PR, so the remaining risk is only "the mock and
real Keycloak disagree in a way this PR exposes", which is narrow.

**Decided (2026-10-06): C + D, plus `workflow_dispatch`.** The path-filtered PR job is
non-required for ~2 weeks, then required once it has run without a flake. The weekly
canary runs against the latest Keycloak and keeps one tracking issue open/updated when it
fails, so there is always a place to look. Pin bumps go through C because they touch the
workflow file. Mitigate pull failures by caching the image (`docker save` into
`actions/cache` keyed by the pinned digest) and by treating a failed pull as an
infrastructure error in the job summary, not a test failure. This replaces the
nightly-only plan from the first draft.

### Q4. Back-channel logout (no decision needed now)

Stays out of scope (`architecture_spec/auth.md` §13 item 7). If it lands later, the emulator
gets a `backchannel_logout_uri` POST in K1.9 and K4.4 gets an "instant kick" row.

## 7. Baseline timings (K0.2)

Measured before K1 on the then-current `test_auth.py`, Release-ish build (RelWithDebInfo, no sanitizers), 4 cores:

| Run | `-m "auth and not slow"` | `-m auth` |
|---|---|---|
| 1 | 29.6 s | 105.3 s |
| 2 | 26.4 s | 101.7 s |
| 3 | 25.9 s | 102.2 s |

After K4 (same machine, no sanitizers): `test_mock_keycloak.py` 7 s; `test_auth.py` fast ~28 s;
`test_auth_keycloak.py` 28 tests ~190 s (the IdP-failure tests wait on the engine's real backoff
timers: 10-35 s each) plus one slow test (62 s). The rest of the e2e suite is ~85 s.

## 8. Outcome: where reality differed from the plan

Everything above landed. What changed on the way, and what real Keycloak taught:

**Run against Keycloak 26.7.5** (Maven Central's `keycloak-quarkus-dist`, `kc.sh start-dev`, in the sandbox
that built this) the same `test_auth_keycloak.py` passes (19 passed, the rest `mock_only`), and
`test_mock_keycloak_matches_real.py` (24 checks) passes. Before calibration the emulator was wrong
about: `invalid_client` (not `unauthorized_client`) for an unknown client; `PKCE verification failed:
Code mismatch`; `No refresh token`; introspection being 403 for a public client; **no `groups` claim at
all for a user in no group**; the ID token's `aud` staying the plain client id under an audience mapper
(the mapper only reaches the access token, or adds *another* audience, which makes `aud` an array);
error redirects carrying `iss` and no description for `unsupported_response_type`; cookies being `Secure;
SameSite=None` even on http; the v2 login theme's message markup. Guesses that were right: "Code not
valid", "Session not active", "Token is not active", "User disabled", the reuse message, the discovery
fields. The docker path (`start()` without `VB_KEYCLOAK_URL`) and `auth_keycloak.yml` have **not** been
run here (no docker daemon, no Actions); the harness logic they share (admin API, proxy, realm import) has.

**Engine changes** (beyond the plan's rule 1b): the sign-in coordinator forgot the stored refresh token
on *any* failed silent refresh, so a Keycloak blip during a re-auth forced a browser sign-in; transient
failures (transport, 5xx, 429, discovery) are now kept and retried (K3.3 asked for exactly this, the
engine did not do it). Also: automation `advance_reauth` (K4.2), client `auth` snapshot + `auth`
predicate, a headless singleplayer client that waits for a test-played sign-in, a `re-auth ok` log line.

**Deviations from the task lists**
- K4.1: `auth_lua()` did not gain `alg`/`audience`; they are properties of the realm (`@pytest.mark.realm(...)`).
- K4.3 `user_cancels_at_idp`: a headless client has no sign-in screen to return to, it exits with
  "sign-in cancelled" (asserted), server never saw `C2S_Auth`, no code was issued. Mock only: a real
  Keycloak form has no cancel.
- K4.3 `disabled_user_cannot_sign_in` / `redirect_uri_mismatch`: no redirect ever reaches the client, so it
  just waits (5 min); the tests assert the IdP's refusal text/400 page, no code exchange, no join.
- K4.3 `audience_array_and_azp` uses `extra_audience` (ID `aud` is an array only when a mapper adds another
  audience); the Q1 access-token case is `test_an_access_token_is_not_a_login` and runs on both backends.
- K4.3 `first_use_trust_prompt` is **not covered at e2e level**: a headless automation client trusts
  automatically and raygui cannot be clicked. The unit test (`coordinator: first use asks for trust`) stands.
- K4.4 `key_rotation_mid_session` is `slow` (62 s): the server refetches the JWKS for an unknown `kid`
  at most once per 60 s counted from startup, in real time.
- K4.4 `idle_session_timeout` sleeps 3 s (the IdP's clock is real); no banner assertion for the idle case
  beyond `reauth_prompt`.
- K5.1: the realm export has no users and no audience mapper; tests create users (`add_user`) and the
  backend adds mappers on request (`audience_mapper`, `extra_audience`, `group_paths`). `full.path` is off.
- K5.2: a recording proxy fronts Keycloak so `requests()`/`grants()` work on both backends.
- K5.3: compares emulator vs Keycloak shapes and error bodies directly; the committed unit fixtures are
  checked against the emulator (`test_the_committed_unit_fixtures_are_what_the_emulator_dumps`).
- K6.3: a job `services:` container cannot take `start-dev --import-realm`, so the harness starts the
  pinned container itself (image cached with `docker save` in `actions/cache`, keyed by tag).

**CI budget.** Under ASan+UBSan the whole e2e CTest entry took 817 s of its 900 s timeout (unit tests 219 s),
so the `slow` tests moved to a second entry, `e2e_wallclock` (`ctest -L wallclock`, 600 s), selected
with `-m slow`; `e2e` runs `-m "not slow"`. Q2's fallback A, for the wall-clock tests only.
