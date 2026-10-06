## Phase 9 — In-Engine Authentication (`auth.lua`)  ✅ 9.0–9.9 done (2026-10-06)

> Full history for this phase; linked from `REMAINING_TASKS.md`. Ground truth for [x] items — do not duplicate here.
> Moved verbatim from `REMAINING_TASKS.md`'s core on 2026-10-06 (dream).

User-requested: a pack-level `auth.lua` declares the identity provider
(generic OIDC, Keycloak, Firebase); its **presence makes authentication
mandatory**. The engine signs the player in and verifies the ID token
server-side during the handshake, before any asset/registry/chunk is sent.
Pack scripts read verified login data via `player:get_login()`, which is
`nil` exactly when the server isn't authenticating. Supersedes the
2026-09-17 §18 Q6 "engine owns no auth concept" direction. Full design,
protocol changes, security notes and per-step task lists:
`architecture_spec/auth.md` §12.

- [x] **9.0 — Decision & docs** (spec §17/§18, networking diagram) — done 2026-10-05.
- [x] **9.1 — `auth.lua` loading & fail-closed startup** — done 2026-10-05:
      `vb::auth::load_auth_lua` (`src/auth/`, new `vb_auth` lib; sandboxed
      return-a-table VM with tight budgets, full validation, oidc/keycloak/
      firebase presets), `server.toml [auth]` overrides, pack loader +
      asset manifest skip root `auth.lua`, `VB_WITH_AUTH` option (refuse to
      start without it), dev-only `--insecure-skip-auth` (compiled out under
      `VB_DISTRIBUTION`), `content/base/auth.lua.example`. **Interim
      fail-closed rule:** until the verifier exists
      (`vb::auth::kVerifierAvailable`, flip it in 9.3) a server whose pack has
      a valid `auth.lua` logs the redacted config and refuses to start unless
      `--insecure-skip-auth`; singleplayer likewise skips such a pack until
      9.7. Tests: `tests/unit/auth_config_test.cpp` plus manifest/loader/
      config cases. `VB_WITH_AUTH` currently only gates this check (Mbed TLS
      + curl arrive with 9.3).
- [x] **9.2 — Protocol v29 + handshake plumbing** — done 2026-10-05
      (v28 was already taken by `S2C_PlayerStatus`, so this is **v29**):
      `AuthMode::kExternal`, `S2C_AuthChallenge`, `S2C_AuthResult.resolved_name`,
      `C2S_Auth.token` 16 KiB cap, `S2C_ReauthRequest`/`C2S_Reauth` wire format
      (used in 9.6), `ByteReader::string(max_len)`. Server FSM `kVerifyingAuth`
      with `HandshakeServerHost::auth_challenge` / `begin_authenticate` →
      `AuthTicket` polled each tick (`ServerHandshake::poll_auth`), per-state
      `timeout_seconds()` (`auth_timeout_seconds`, default 300, age reset once
      sign-in finishes), fail-closed when no challenge/verifier. Client FSM
      `kAwaitingChallenge` + `HandshakeClientHost::obtain_token` (synchronous
      until 9.5). Tests: protocol round-trip/bounds/truncation, FSM cases with
      a fake verifier in `net_test.cpp`. **Not yet wired:** `server/main.cpp`
      still never sets `auth_mode = kExternal` (9.3 supplies the real verifier);
      a full loopback ServerSession join with the fake verifier is not
      covered, only the FSMs.
- [x] **9.3 — Server token verification** — done 2026-10-05: `VB_WITH_AUTH`
      now pulls libcurl (hoisted from the CLI block) and **Mbed TLS 3.6.2**
      (`vb_fetch`). `vb_auth` gains `jwt` (base64url, compact JWS),
      `crypto` (RS256 ≥2048-bit / ES256 P-256 verify, SHA-256, OS entropy,
      test-only `TestSigner`), `jwks` (RSA n/e + EC x/y, skips unusable keys),
      `http` (`HttpFetcher` + curl impl, https/loopback only, 1 MiB cap),
      `verifier` (`verify_id_token`, rules 1–7, `LoginInfo`, coarse
      `public_reason`) and `service` (`AuthService`: OIDC discovery with
      issuer check, JWKS prefetch thread with backoff, one in-flight fetch,
      ≥60 s unknown-`kid` refresh limit, pollable `Pending` verification,
      Firebase preset). `kVerifierAvailable = kBuiltWithAuth`; the server
      now starts on an auth pack (`auth_mode = kExternal`, challenge nonce per
      connection, ticket → verdict → pack join veto with the resolved name).
      Tests (`auth_verifier_test.cpp`): every rule has pass + fail cases,
      JWKS cache/refresh/rate-limit via a fake fetcher, mutation fuzz,
      redaction. ~~Not verified: a real Keycloak token~~ — verified in 9.9
      (Keycloak 26.7.5, 2026-10-06); libFuzzer targets (a deterministic mutation test stands in).
      **Known gap until 9.4:** the verified `LoginInfo` is not yet kept on the
      session, so `get_login()` and duplicate-subject handling don't exist yet.
- [x] **9.4 — Lua exposure** — done 2026-10-05: `net::LoginData` (user data
      only), `AuthOutcome::login`, FSM `finish_external_auth` →
      `HandshakeServerHost::resolve_name` (session suffixes collisions:
      `alex`/`alex#2`) → `join_veto` (the pack's `player_join(name, login)`,
      frozen login table or `nil`). `ServerSession::player_login`,
      duplicate `(issuer, subject)` kicks the older session right after the
      newcomer verifies, `SessionPlayerJoined::login`. `PackRuntime`:
      `player:get_login()` (frozen proxy, locked metatable, cached per
      player, kept until after `player_leave` so every `Player` has a login),
      `vb.auth.required()`, `set_auth_required()`. `docs/lua-api.md`
      "Authentication". Tests: `tests/unit/auth_login_test.cpp` (loopback
      server+clients with a fake verifier: frozen/non-nil login, plain pack ⇒
      nil, rejected token ⇒ never joins, veto on claims, name suffixing,
      duplicate kick). **Moved to 9.5:** `state.login` in the client UI VM
      (the client only learns its own subject once it holds the token), and
      a base-pack example script (the `lua-api.md` snippet covers it).
- [x] **9.5 — Client sign-in** — done 2026-10-05 (UI compile-checked, not
      run against a real IdP): `vb_auth` gains `oidc_client` (discovery with
      issuer check, PKCE S256 per RFC 7636 incl. its test vector,
      authorization URL, code exchange/refresh, redirect parsing with `state`
      check, 127.0.0.1-only one-shot `LoopbackListener`, `oidc_browser_sign_in`,
      shell-less `open_system_browser` for Linux/macOS/Windows), `firebase`
      (Identity Toolkit password sign-in + refresh, coarse error mapping),
      `SignInTask` (worker thread), `SignInCoordinator` (UI-facing state,
      retryable failures, cancel) and `read_token_file`. `HttpFetcher::post`.
      Handshake: `ClientHandshakeStatus::kSigningIn`, `TokenTicket` /
      `HandshakeClientHost::begin_sign_in`, `ClientHandshake::poll()` /
      `cancel_sign_in()`, `ClientSession::set_sign_in_provider()`.
      Client: engine-drawn `MainMenu::draw_signing_in` (browser button,
      firebase e-mail + masked password, provider host shown, Cancel),
      connect deadline held off while signing in, `--auth-token-file`
      (automation builds only, re-read per sign-in). Tests:
      `auth_signin_test.cpp` (PKCE vector, URL, redirect/state, code exchange,
      full browser flow over a real loopback socket incl. stray requests,
      denial/timeout/cancel, Firebase, coordinator retry, token file).
      **Not done / moved:** Firebase `google` sign-in (needs a Google OAuth
      client id that `auth.lua`'s firebase preset has no key for; the screen
      only offers `password`); `state.login` in the client UI VM; manual
      sign-in against real Keycloak/Firebase on Linux/macOS/Windows; Windows
      socket/ShellExecute code is written but unbuilt.
- [x] **9.6 — Sessions, re-auth & revocation** — done 2026-10-05
      (client UI compile-checked, not run against a real IdP).
      Server: `ServerSession::system_reauth` — per-player jittered (±10%)
      `S2C_ReauthRequest{nonce, grace}` every `reauth_interval_seconds`,
      answer verified through the same verifier (`C2S_Reauth`, only accepted
      for an outstanding request), same `(issuer, subject)` required (else
      immediate kick), `iat ≥ request_sent − 60 s` (so a stale token can't
      answer), kick with `kAuthFailed "sign-in expired or revoked"` when the
      grace runs out, new `LoginData::issued_at/expires_at`,
      `HandshakeServerConfig::reauth_*`, `HandshakeServerHost::unix_time`.
      Changed allowlisted claims swap the login and fire
      `vb.on("login_changed", function(player, login))` once
      (`SessionLoginChanged`, `PackRuntime::dispatch_login_changed`). Client:
      `SessionStore` (refresh tokens per `(issuer, client_id)` in
      `user_config_dir()/auth/<sha256>.json`, created 0600, atomic write; trust
      list per `(server, issuer)`), `SignInCoordinator` — first-use trust
      prompt (nothing, not even a cached refresh token, goes to the IdP before
      it), silent refresh at join, silent answer to re-auth requests, otherwise
      a non-blocking "Your sign-in expired — Sign in again" banner + overlay
      (browser/password, bound to the request nonce), dead refresh tokens are
      forgotten; `ClientSession::set_reauth_provider`; main-menu "Signed in as
      … / Sign out" (top-right). Tests: 6 server re-auth cases in
      `auth_login_test.cpp` (success ×2 rounds, claim change fires once,
      revoked ⇒ kicked after grace, stale iat, different subject, disabled),
      store/trust/silent/dead-token/re-auth in `auth_signin_test.cpp`.
      ~~Not verified: a real Keycloak admin logout kicking within interval +
      grace~~ — verified in 9.9 against Keycloak 26.7.5 (admin logout, disabled
      user, idle session, key and refresh-token rotation; `advance_reauth`
      drives the re-auth timers).
- [x] **9.7 — Singleplayer, `vb`, e2e** — done 2026-10-05 (client UI
      compile-checked; e2e run result below).
      Singleplayer with an auth pack now runs the real sign-in through the same
      handshake (`vb::auth::install_external_auth` / `apply_external_auth` are
      shared with the dedicated server; the integrated server ticks in real
      time while the sign-in screen is up); `--insecure-skip-auth` on the
      client for dev (compiled out under `VB_DISTRIBUTION`). `vb server config
      <name> set auth.issuer|client_id|project_id|api_key` edits the `[auth]`
      table (root keys are now always inserted above the first table). e2e:
      `tests/e2e/vbtest/mock_idp.py` (stdlib OIDC provider: discovery, JWKS,
      auth endpoint → loopback redirect, PKCE-checking token endpoint,
      refresh grant; pure-Python RS256 with the test-only key in
      `tests/e2e/fixtures/`), `tests/e2e/test_auth.py` (token-file join under
      the verified name, login visible to pack scripts, 6 bad-token classes
      never join, missing token / no auth support fail closed, duplicate
      account kicks the older session, `alex`/`alex#2`, the browser flow with
      PKCE over a real loopback redirect (headless client writes the
      authorization URL to `VB_AUTH_URL_FILE`; automation builds only),
      revoked login kicked after the grace), automation predicate
      `player_login` + `players[].login` on the server.
- [x] **9.8 — Hardening** — done 2026-10-05: per-IP sign-in rate limit
      (`ServerSession::set_max_auth_attempts_per_minute_per_ip`, default 30/min,
      tested with a fixed-IP transport decorator), single in-flight JWKS fetch
      + backoff (9.3), security-table row, operator guide `docs/auth.md`.
      **Not done:** libFuzzer targets (a deterministic mutation test covers
      JWT/JWKS/verifier; the protocol decoders have truncation tests), OS
      keychain storage for refresh tokens (0600 files today), Firebase
      `google` sign-in (needs a Google OAuth client id key in `auth.lua`),
      manual Keycloak/Firebase runs on Linux/macOS/Windows, Windows build of
      the socket/ShellExecute code.
- [x] **9.9 — Auth testing against a mock Keycloak** — done 2026-10-06
      (plan, decisions and deviations: `docs/auth-keycloak-testing.md`).
      `vbtest/mock_keycloak.py` is a stdlib Keycloak emulator (users, SSO
      sessions, Keycloak claims and error bodies, RS256/ES256, key rotation,
      login form, fault injection, request log), self-tested without game
      binaries and **checked against a real Keycloak** (`--vb-idp=keycloak`,
      `test_mock_keycloak_matches_real.py`, CI `auth_keycloak.yml`: auth PRs +
      weekly canary); running the same `test_auth_keycloak.py` against Keycloak
      26.7.5 closed the "not verified against a real Keycloak" gaps of 9.3/9.6.
      Engine: **rule 1b** (`typ == "ID"` for the keycloak preset, `at+jwt`
      refused everywhere, `VerifyError::kTokenType`), an IdP outage no longer
      erases the stored refresh token or raises the banner at once (retryable
      failures are retried every 5 s while the re-auth request is open),
      automation `advance_reauth`, client `auth` snapshot + predicate, headless
      singleplayer waits for a test-played sign-in, `re-auth ok` server log
      line. Golden fixtures for C++ unit cases (`tests/unit/fixtures/keycloak/`,
      `auth_keycloak_test.cpp`, `auth_keycloak_client_test.cpp`). Found against
      real Keycloak: no `groups` claim for ungrouped users, `invalid_client` for a
      bad client id, the ID token's `aud` stays the plain client id under an
      audience mapper. **Not done:** first-use trust prompt at e2e level (a
      headless client trusts automatically, and raygui cannot be clicked; the
      unit test stands), Windows/macOS runs.
