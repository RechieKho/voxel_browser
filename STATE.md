# STATE — Working Notes for Future Agents

> Living scratchpad of gotchas, landmines, open decisions, and small TODOs
> discovered while working in this repo. **Update this file** when you learn
> something non-obvious or fix something listed here. Keep this file lean
> (~300-400 lines) — new verbose writeups belong in `state/*.md`, linked from
> here, not pasted inline. See "Detail files" at the bottom for the index.
> **Hard cap: 500 lines** — over that, "dream" (move inactive entries into
> `state/*.md`) before ending the session; see `DREAMING.md`.
>
> Companion docs: `ARCHITECTURE_SPEC.md` (target design) · `REMAINING_TASKS.md`
> (implementation backlog). This file is for *traps and context*, not the plan.
>
> **If what you learned is tied to a specific physical machine** (a tool
> path, an agent-shell/Bash-vs-PowerShell quirk, which pre-built `build-*`
> dirs exist, a local toolchain oddity) **write it to `STATE.md.local`
> instead of here** — see that file's own header for why. This file (and
> `state/*.md`) is for gotchas that hold regardless of which machine an
> agent is running on.

---

## Standing priority: prefer e2e verification over reading code or unit tests alone

When fixing anything a player would actually see or feel (HUD layout, menu
screens, physics-driven policy like fall damage, anything behind
`client.screen_size()` or raw pixels), **drive it through the real windowed
automation harness (`tests/e2e`, `docs/e2e-automation.md`) and look at an
actual screenshot before calling it fixed**, not just reasoning about the code
or trusting a unit test that synthesizes the inputs. Concrete misses from one
session (2026-10-05) that code-reading and unit tests alone did not catch:

- A HUD scaling fix *looked* right by inspection but silently drew nothing in
  a real client — `build-e2e` was a stale binary built before a
  `client.health()` Lua binding it depended on had landed, so every HUD frame
  threw a Lua error. Only a real screenshot surfaced the empty HUD; nothing
  about the Lua or C++ diffs themselves would have shown it.
- A status-bar width fix (anchoring to the full hotbar row instead of however
  many slots are filled) introduced a new overflow at the engine's own
  enforced minimum window width — caught by screenshotting a fully-stocked
  9-slot hotbar at that floor, not by re-reading the arithmetic.
- `tests/unit/content_base_behaviour_test.cpp`'s fall-damage test drives
  `player_landed` by setting velocity directly (`BasePackFixture::land()`),
  which exercises the *policy curve* but not the actual jump-launch-speed /
  landing-speed round trip through real physics — the real bug (jumping on
  flat ground took damage because `SAFE_SPEED` sat below the engine's own
  `jump_speed`) needed a real `key.press("jump")` through a real client to
  reproduce and confirm fixed.
- That same e2e jump test raced its own assertion: checking health
  immediately after `on_ground` flips back to `true` missed the damage,
  which lands a tick or two later — confirmed by hand that an immediate
  check silently passed against the *known-buggy* code. Absence-of-an-event
  assertions need a real settle window; presence-of-an-event ones should use
  `wait_for`/`expect` as usual (no fixed sleeps there).
- Raw input automation (`key.press`/`key.hold`) does **not** auto-capture the
  mouse the way `walk_to`/`break_block`/`select_slot` do (by design — see
  `docs/automation-protocol.md`'s `mouse.capture` row) — a test driving
  movement/jump via raw `key.press` silently no-ops (zero position change,
  `on_ground` never changes) until `client.capture_mouse()` is called first,
  same as a real player who hasn't clicked the window.

None of these would have been caught by re-reading the diff more carefully or
by the existing unit-test suite; they needed a real client process, a real
screenshot, or a real multi-tick wait. Treat "I reasoned through the code and
it looks right" as a hypothesis, not a result, for anything in this category.

---

## Standing priority: prefer engine (C++) work over content (Lua)

When picking what to work on next and multiple open items are roughly equally
ready (e.g. choosing among REMAINING_TASKS' several `[ ]` items with no other
constraint forcing one), **prioritize a task that changes the engine (C++
code under `src/`/`inc/`) over one that's purely `content/*.lua` policy/
content work.** Most remaining Lua-only gaps (PvP/mob damage policy, hunger,
more base-pack art/reskins, etc.) are content authors' problems the engine
already has the primitives for; the engine surface itself is where real
mechanism gaps still exist and where a wrong call is much more expensive to
unwind later. This is a standing preference for ambiguous "what next"
choices, not a rule to force an engine change where a task is inherently
content-only (e.g. the user explicitly asks for a content-pack feature).

---

## Current status (2026-10-02)

**E2E automation: designed, E0 through E6 landed (all phases).** `docs/e2e-automation.md` is the
design for a Playwright-style multiplayer test harness (stdio JSON-lines
driver in the client/server + a pytest `vbtest` package). Hard constraint:
the automation driver must be **compiled out of production** —
`VB_WITH_AUTOMATION` defaults `OFF`, a `VB_DISTRIBUTION` build must refuse it
at configure time, CI checks `--version` for `+automation`, and servers built
without it reject automation clients in the handshake (§7). That protects
against *our* tooling reaching production, not against third-party bots;
flood protection stays server-side. Landed so far: E0 only — the
`InputSource`/`InputFrame` seam (`inc/vb/render/input.hpp`,
`src/render/input.cpp`, `tests/unit/input_test.cpp`); `sample_input_cmd` and
`MovementBindings` moved there from `src/client/main.cpp`. E1 (2026-10-02): the windowed loop moved
verbatim out of `src/client/main.cpp` into `ClientApp` (`client_app.{hpp,cpp}`;
`session_host.hpp` holds `Singleplayer`/`RemoteConnection`), and `--headless` is
now `ClientApp(render=false)` + `connect_blocking()` instead of a separate loop
(headless skips the menu, loading screen, GL resources and `client.toml`
writes; smoke output unchanged). Verified only headless — the windowed path
was not run (no GPU in the container), so give it a manual smoke before
trusting it. E2 (2026-10-03): `VB_WITH_AUTOMATION`/`VB_DISTRIBUTION` options
(distribution+automation is a configure error; release/`debug` CI legs and the
macOS/Windows legs set `VB_DISTRIBUTION=ON` and grep `--version` +
`--automation stdio` of the staged binaries), `+automation` in `describe_build()`,
and `src/automation/` (`vb_automation`: JSON-lines `Host`, predicate engine,
protocol; endpoints in `src/{client,server}/automation_endpoint.hpp`).
`--automation stdio` works on the server and on `--headless` clients
(`hello/state/step/quit/wait_for`, `--automation-clock real|manual`); contract in
`docs/automation-protocol.md`. Gotchas: GNS has no `--port 0`; protocol uses
raw fd 0/1 (see design §10 items 7-8). No
**Implementing any E-phase? Follow `docs/e2e-automation.md` §11**
(per-phase doc checklist + safety invariants to re-verify) before calling it done. E2b (2026-10-03): `C2S_Hello.client_flags` (protocol 26→27); automation builds set `kClientFlagAutomation`, and a server without automation refuses such clients in the first handshake step (`HandshakeServerConfig::accept_automation_clients`). Tests: 4 new `net_test.cpp` cases + a `protocol_test.cpp` round-trip; the loopback-only guard in `vbtest` is E4. E3 (2026-10-04): client `key/mouse/look/select_slot/walk_to/break_block/place_block/chat.send/ui.*/hud.*` (`src/client/automation_endpoint.{hpp,cpp}`; multi-frame actions answer via deferred replies, `Host::respond`) and server `set_block/fill/teleport/give/set_time/set_health/kick/run_lua/block_at` (`src/server/automation_endpoint.hpp` + `#if VB_WITH_AUTOMATION` primitives on `ServerSession`/`PackRuntime`). Verified by hand with a throwaway script against a real `VB_WITH_NET` dedicated server + two real headless clients (break/place replicate to the other client, give/teleport/chat/walk/kick), 35 checks, plus the automation-off and -on `vb_tests` (430 / 440). NOT run: CI, Windows/macOS. Gotchas found: design §10 items 10-12 (`World::set_block` makes chunks; singleplayer has no pack keybinds; headless skips UI eval). E4 (2026-10-04): `tests/e2e/` (pytest `vbtest` package: sync API, `server`/`clients` fixtures with per-client cold asset caches, `expect()` auto-waiting, artifacts on failure, loopback-only guard), 10 tests, `ctest -L e2e` (registered only with AUTOMATION+NET+LUA+COMPRESSION and pytest importable), and an `e2e` job in `build_linux.yml` (ASan+UBSan, `VB_E2E_TIMEOUT_SCALE=3`; failure logs upload as `e2e-failure-logs`, deliberately not `<project>-*`). New predicates `chunk_loaded`, `on_ground`; E3's `break_block`/`place_block` fixed to send one click (they over-clicked). **Fixed a real engine crash: closing a pack UI with `ui.close()` while connected segfaulted the client** (`UiRuntime::do_close` passed a state-less nil to `lua_to_json`; regression test added). Verified locally: `ctest -L e2e` 10/10 five runs in a row (~26 s), full ctest in the NET tree, `vb_tests` 431 (off) / 443 (on) / 448 (on+NET), whole tree clean under `-Werror`. **NOT run: the CI job itself (never on a runner; ASan/LSan may find things), Windows/macOS.** Gotchas: design §10 items 13-19 (spawn is a drop; look applies after the input hook; punches hit players first; CI never compiled the automation code before). E5 (2026-10-04): `--net-sim` on both binaries (`inc/vb/net/net_sim.hpp`, `GnsTransport::set_net_sim`; refused in production builds, with `--singleplayer`, and without `VB_WITH_NET`), `rtt_ms` in the client snapshot, `@pytest.mark.net_sim`; windowed automation (no `--headless`, needs a display): `menu.set_name/connect/singleplayer`, `screenshot` (PNG), `type` (chat box, also headless); `vbtest.traceview` writes `trace.html` for every failed test; the CI `e2e` job now runs under `xvfb-run`, and all three release legs plus CTest check that `--net-sim` is refused. Verified locally: 18 e2e tests, 3 runs in a row under Xvfb (~44 s), 14 + 2 skips without a display, `vb_tests` 431 / 444 / 449, whole tree clean under `-Werror`; screenshots looked at (menu with the name filled in; the world with HUD). **NOT run: the CI job itself, Windows/macOS.** Gotchas: design §10 items 20-23 (raylib batches draw calls; windowed "joined" is not "playing"; GNS sim is process-wide). E6 (2026-10-04): `--automation tcp[:PORT]` (`src/automation/tcp.cpp`; binds 127.0.0.1 only, random token as the first line, `--automation-token`/`--automation-info`, one connection at a time, the game survives a disconnect); `--automation-record out.py` (`src/client/recorder.*`, a `ClientApp::Observer`; writes a runnable vbtest script with assertions after observable results); `vbtest.stack` + `clients(tcp=True)`/`detach`/`reattach`. Verified locally: 24 e2e tests (~60 s under Xvfb), a recorded session replays green on a fresh server (3x), the listener's bind address read from `/proc/net/tcp`, `vb_tests` 431 / 446 / 451, production tree passes all 7 flag-refusal checks, whole tree clean under `-Werror`. **NOT run: the CI job on GitHub (a feature-branch push triggers nothing: open a PR, push to a `*-workflow` branch, or dispatch manually), Windows (Winsock code is unbuilt), macOS.** A local ASan+UBSan+Xvfb run of the exact CI configuration found and fixed several CI-killing problems (design §10 items 28-31) and is now green twice. Gotchas: design §10 items 24-27. The remaining work is verification, not features: run the CI `e2e` job once, and build on Windows/macOS.

## Status (2026-09-30)

**Bumped doctest `v2.4.11` -> `v2.5.3`; `CMAKE_POLICY_VERSION_MINIMUM=3.5` shim
stays.** REMAINING_TASKS.md's Phase 0 backlog said to drop the shim "if
doctest is bumped" — bumped it (`cmake/Dependencies.cmake`), then actually
tried removing the shim and rebuilding `build-net-lua`'s `vb_tests` target to
check. It failed, just not on doctest: lz4 `v1.9.4` (only pulled in behind
`VB_WITH_COMPRESSION`, which `build-net-lua` has on) independently declares
`cmake_minimum_required(VERSION 2.8.12)`, which CMake >= 4 (this machine's
`scoop`-installed cmake) rejects the same way doctest's old `VERSION 3.0` did.
So the shim's own comment was too narrow — restored it with a note that it's
gating lz4 now, not doctest, and will only fully drop once lz4 (or whatever
old-floor dependency remains) is bumped too. Verified end to end: full
`vb_tests` rebuild + run green (426 test cases, 134460 assertions) with
doctest v2.5.3 and the shim back in place. Full note in
`remaining_tasks/phase0.md`.

**Wired `VB_WITH_NET` into macOS CI — unverified, needs a real Actions run.**
`build_macos.yml` builds a universal (arm64+x86_64) binary, but was skipping
`VB_WITH_NET` because brew's protobuf/OpenSSL are single-arch and GNS needs
both. Added a `build_net_deps` job (runs once, ahead of the release/debug
matrix) that builds protobuf v21.12 and OpenSSL 3.3.2 twice each — once
per arch via `-DCMAKE_OSX_ARCHITECTURES=<one arch>` / `Configure
darwin64-<arch>-cc` — then `lipo -create`s the resulting `.a` files together
into one universal install prefix (keeping the arm64 pass's protoc binary,
CMake package, and OpenSSL headers, since those are arch-independent text/
tool artifacts). Uploaded as an artifact; the `build` matrix downloads and
unpacks it, then passes `-DCMAKE_PREFIX_PATH=<prefix> -DOPENSSL_ROOT_DIR=
<prefix> -DOPENSSL_USE_STATIC_LIBS=ON -DVB_WITH_NET=ON`. Picked protobuf
v21.12 specifically because it predates protobuf's Abseil dependency
(landed v22) — an Abseil dependency would mean building *that* universal
too, since its installed CMake config is a transitive `find_dependency()`
of protobuf's own. Picked v21.12/openssl-3.3.2 as real, existing upstream
tags (`git ls-remote --tags` confirmed both before pinning). A single
`-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"` pass (like the rest of this
project's deps use) was deliberately **not** attempted for protobuf/OpenSSL:
this repo's own pre-existing comment in `build_macos.yml` already flagged
that as unreliable for protobuf, and OpenSSL's `Configure` script has no
multi-arch mode at all (one target triple per invocation) — dual-build +
`lipo` is the only option for OpenSSL regardless. GNS's `USE_CRYPTO` needed
no code change: default is already `"OpenSSL"` on non-Windows (`libsodium`
was ruled out — GNS's own CMakeLists fatal-errors it outside x86/x86_64 via
a `CMAKE_SYSTEM_PROCESSOR` check that isn't universal-build-aware; there is
no dependency-free "Reference" option for the AES/SHA256 backend in GNS
v1.6.0, only for the separate 25519 backend). **Not run against real
GitHub Actions** — this agent environment has no macOS runner. Verified
what could be verified locally/via network: `build_macos.yml` parses as
valid YAML (`python -c "import yaml; yaml.safe_load(...)"`), both pinned
git tags resolve via `git ls-remote`, and the CMake target names/option
names/install-guarding referenced (`libprotobuf`, `libprotobuf-lite`,
`protobuf_BUILD_PROTOC_BINARIES`, `protobuf_BUILD_SHARED_LIBS`,
`protobuf_WITH_ZLIB`) were confirmed against a real clone of
`protocolbuffers/protobuf` at `v21.12`. If the first real run fails, the
likely first suspects: (1) whether `cmake --install` on the x86_64-only
protobuf pass errors instead of silently skipping protoc/libprotoc install
rules when `protobuf_BUILD_PROTOC_BINARIES=OFF` (`cmake/install.cmake`
appeared to gate them correctly by reading, but was never actually run);
(2) whether OpenSSL 3.3.2's `opensslconf.h`-family headers are truly
byte-identical between the two Configure targets (assumed, not diffed
against a real build); (3) GitHub-hosted `macos-latest` actually being
Apple Silicon (arm64) as assumed, making the "arm64 = native, x86_64 =
cross" split correct — if that ever flips, swap which pass keeps
protoc/headers.

**Closed the `WorldReplicator` uncapped-send-burst gap** (§6 below has the
full writeup): a new `set_send_budget_bytes(std::size_t)` caps
`S2C_ChunkAdd` bytes/tick per player, wired to a new `server.toml` key
`chunk_send_budget_bytes_per_tick` (0 = unlimited, matching every other
budget knob). New regression test in `world_replication_test.cpp`. Full
`vb_tests` 426/426 green on `build-net-lua`.

**Root-caused and fixed the `PlayerHandle`-stashed-across-ticks bug**
(REMAINING_TASKS.md's Cross-Cutting item, open since 2026-09-28 — full
history now in `state/changelog-part3.md`, see below). The 2026-09-29
"zero-argument call shape" theory was a red herring, ruled out by two
decisive experiments: a second repro using `vb.every(...)`'s timer (called
with *zero* Lua arguments at all, not just zero non-handle scalars) crashed
identically to the `tick` event, and stopping Lua's GC entirely
(`lua_gc(L, LUA_GCSTOP, 0)`) did **not** prevent the crash, ruling out
premature collection. The real cause, found by temporarily printing raw
pointers from inside `PlayerHandle::get_name()` and `Impl::run_chat()`: the
`self` extracted for the crashing call was **byte-for-byte identical to
`&p`** of `run_chat()`'s own local `PlayerHandle p{...}` — the Lua value
being read wasn't a copy of the handle, it was a raw pointer into an
already-popped C++ stack frame. sol2 (`stack_core.hpp`'s
`stack_detail::push_reference<T>`) pushes a non-const lvalue reference to a
registered usertype as a pointer into existing memory (no copy) by default,
a documented perf optimization, unless `SOL_FUNCTION_CALL_VALUE_SEMANTICS`
is defined on — every `PlayerHandle` dispatch call site constructs a named
local and passes it straight into the Lua call (an lvalue every time), so a
pack script that stores that argument beyond the call (storage location
never mattered, exactly as 2026-09-28 found) holds a dangling stack pointer
the instant a *different* C++ call path reuses that address — explaining
both why storage location never mattered and why reading it back from a
*later, separate* `chat`/`player_input` dispatch "worked" (same call shape
happens to re-populate the same stack address with correct-looking values)
while `dispatch_tick`'s entirely different call tree did not. **Fix**
(`cmake/Dependencies.cmake`, right after `sol2`'s `vb_fetch()`):
`SOL_FUNCTION_CALL_VALUE_SEMANTICS=1` defined on the real `sol2` target
(resolved off the `sol2::sol2` alias via `ALIASED_TARGET`, since
`target_compile_definitions()` rejects alias targets) — forces every
registered-usertype function-call argument to push as an owned copy
regardless of value category. Safe project-wide: `PlayerHandle` is the only
usertype this project registers and is a stateless proxy (no method mutates
`net_id`/`rt` in place), so this changes nothing observable. New permanent
regression test in `pack_runtime_integration_test.cpp` ("a PlayerHandle
stashed from a chat handler survives being read back from a later
tick/timer handler") exercises both the `tick`-event and `vb.every` timer
shapes that used to crash. `content/base/entities/zombie.lua`'s
`player_input`-driven workaround was left as-is (comment updated to stop
describing a now-fixed bug as current) — it never actually stored a
`PlayerHandle`, only the zombie's own entity handle keyed by player name, so
there was nothing to migrate. Full root-cause writeup and the empirical
pointer-identity proof in `remaining_tasks/cross_cutting.md`'s 2026-09-30
follow-up. Verified: full `vb_tests` 425/425 green on `build-net-lua` (plain
rebuild) and 403/403 green on `build-asan-repro` (ASan on, exit 0 — no new
leaks/UB), clean `-Werror` build of `vb_tests`/`voxel_browser`/
`voxel_browser_server` (temporarily reconfigured `build-net-lua` with
`-DVB_WARNINGS_AS_ERRORS=ON`, confirmed clean, reconfigured back to this
dir's OFF default afterward).

Before that: full reverse-chronological detail for everything back through
6.18 (discrete punch combat) -- including "Same-day follow-up #3" (the third
loading-screen-dismissing-over-an-empty-world fix) and the original
`PlayerHandle` corruption investigation this section's fix above closes out
-- moved to `state/changelog-part3.md` to keep this section within budget;
see that file's own header for the full topic list, or "Detail files" below.

---

## 0. Repo snapshot

- `674d88f Initial commit` + the Phase 0 restructure (uncommitted at time of
  writing). Still **no tags**. Remote: `https://github.com/RechieKho/voxel_browser.git`.
- Source tree matches spec §4: `vb_core` (`src/core/`), `vb_render`
  (`src/render/`), `voxel_browser` (`src/client/`), `voxel_browser_server`
  (`src/server/`), `vb_tests` (`tests/`). Other module dirs are `.gitkeep` stubs.
- Builds green on Windows/clang with `-DVB_WARNINGS_AS_ERRORS=ON`.
- `ARCHITECTURE_SPEC.md` / `REMAINING_TASKS.md` are still design intent for
  Phase 1+.

---

## 1. Build-blocking bugs — ✅ all fixed in Phase 0

All four were fixed during the Phase 0 restructure (2026-09-10):
version info moved to a generated `vb/core/version.hpp`; raylib pinned to
`5.5` (not `6.0` — that tag never existed) with raygui matching `4.0`;
`cmake_minimum_required` bumped to `3.25`; a `CMAKE_POLICY_VERSION_MINIMUM
3.5` shim in `cmake/Dependencies.cmake` works around CMake ≥ 4.0 rejecting
doctest 2.4.11's `cmake_minimum_required(VERSION 3.0)` (remove the shim
once doctest ships a fix). Full detail in `state/changelog-part1.md`'s
2026-09-10 entries.

## 2. Naming — ✅ resolved

`PROJECT_NAME` is `voxel_browser`. Executables: `voxel_browser` (client),
`voxel_browser_server`. CI artifacts are
`voxel_browser-<target>-<arch>-<build_type>`.

---

## 3. CI landmines

- **No tags exist.** `setup_metadata.yml` runs `git describe --tags
  --abbrev=0` for the `version` output — this **errors** with no tags in
  history. `bundle`/`publish` depend on `setup_metadata`; first `git tag
  v0.0.1` will unblock. Until then anything past `build_*` is untested /
  likely red.
- `publish.yml` triggers only on `v*.*.*` tags and pulls artifacts from
  `runner.yml` by name `${project_name}` (note trailing space in the YAML
  on `name:` line 26 — `action-download-artifact` may or may not trim it).
- `lint.yml` installs clang-format via `pip install` then runs it via
  `pipx run clang-format` (dead weight from the pip install). Lints `src/**`
  with `--Werror` — every new file under `src/` must be clang-format-clean.
- Build workflows use `actions/checkout@v4 submodules: recursive`, but
  **there are no submodules** — deps are `FetchContent`. Harmless.
- `build_linux.yml` installs X11/GL dev packages for raylib, plus
  `libssl-dev libprotobuf-dev protobuf-compiler` for `VB_WITH_NET`.
- **`VB_WITH_NET` CI coverage is uneven:** Linux (apt) and Windows (vcpkg)
  build it; **macOS does not** (single-arch Homebrew protobuf vs. the
  universal arm64+x86_64 build — see `state/dependencies-detail` pointer
  below). If macOS ever gets it, `build_macos.yml`'s configure step is
  where to add it.
- **Never call `find_package(Protobuf REQUIRED)` a second time anywhere in
  the tree.** GameNetworkingSockets' own `src/CMakeLists.txt` already calls
  it; a second call (even as a "fail fast" convenience) fatal-errors on
  some protobuf installs (Homebrew, not vcpkg) with "Some (but not all)
  targets in this export set were already defined." Fixed 2026-09-11 by
  deleting the redundant call — don't reintroduce it.
- **Machine-local build/toolchain/agent-shell gotchas (vcvars64.bat path,
  Bash-vs-PowerShell-tool quirks, which pre-built `build-*` dirs actually
  exist, macOS-specific findings) live in `STATE.md.local`, not here** —
  gitignored (`*.local`), specific to whatever physical machine an agent
  session runs on; check it first when setting up a build in an agent
  shell, and add to it rather than here when you hit a new one.

---

## 4. Config / style quirks

- `.clang-format` is **Godot's** clang-format file verbatim. The C++ rules
  that matter: **tabs** (`UseTab: Always`, `TabWidth 4`), `ColumnLimit: 0`
  (no auto-wrap), `AccessModifierOffset: -4`, pointers right-aligned,
  `Cpp11BracedListStyle: false`. `Standard: c++17` but CMake sets
  `CMAKE_CXX_STANDARD 20` — fine unless C++20-only syntax confuses the
  formatter; bump to `c++20` if that happens.
- `.gitignore` ignores `build`, `*.local`, `compile_commands.json`,
  `.vscode/*` (except `extensions.json`). `CMAKE_EXPORT_COMPILE_COMMANDS
  ON` is set — symlink/copy `build/compile_commands.json` to root for
  tooling.
- `CMakeLists.txt:54` uses `file(GLOB_RECURSE SOURCE_FILES ...)` — adding a
  `.cpp` doesn't trigger reconfigure. Keep `cmake` re-runs in the loop.
- `libfantastic` (the lib target) is `add_library(... INTERFACE)` — assumes
  header-only. `vb_core` is a real STATIC lib; don't carry the INTERFACE
  assumption forward.
- `.gitignore`/`tests/CMakeLists.txt` are checked in with CRLF endings
  (every other text file is LF-only) despite `core.autocrlf=input` locally
  — `git add` warns harmlessly today, but the first real edit through a
  CRLF-preserving tool will silently flip the whole file's line endings,
  burying the actual diff. No `.gitattributes` forces this repo-wide. If
  ever cleaned up, use `git add --renormalize <path>` after adding a
  `.gitattributes` rule, not a manual find/replace.

**Longer-form gotchas (compiler/toolchain traps, full detail in
`state/gotchas.md`):**
- `std::erase`/`std::remove` on a `std::vector<ChunkCoord>` (or any small
  trivially-copyable struct that size/alignment shape) fails to compile
  under this repo's clang-targeting-MSVC-STL toolchain
  (`static_assert(false, "unexpected size")` inside `<xutility>`). Use a
  manual erase loop instead.
- A local `clang-format --dry-run --Werror` binary may not be trustworthy
  as-is against this repo's `.clang-format` — verify by diffing against an
  unmodified `HEAD` file before trusting either a pass or a wall of
  violations.
- `doctest`'s `--test-case=` filter is a **glob pattern**, not a substring
  match — wrap in `*...*` and quote it (zsh glob-expands an unquoted
  pattern itself).
- MSVC (`cl.exe`) rejects a ternary between two different instantiations of
  a templated smart-pointer type with converting constructors (`C2445`) —
  use plain `if`/`else` instead. Clang/GCC not cross-checked.
- A sibling file's "isn't implemented yet" comment can go stale the moment
  a later phase lands it, without the comment ever being updated — grep the
  actual current binding (e.g. `src/script/pack_runtime.cpp`) before
  reusing a sibling's "this doesn't work yet" framing in new code.
- A client built without `VB_WITH_COMPRESSION` can never asset-sync
  against a server built with it on (`hash_bytes()` stubs to all-zero) —
  every real (non-`--singleplayer`) connection needs **matching**
  `VB_WITH_COMPRESSION` on both binaries; there's no runtime negotiation.
- A second, independent bug produces the identical `"asset transfer failed
  (hash mismatch or size cap)"` text even with matching compression flags:
  the server used to build the asset manifest *before* `vb.storage`'s
  deferred first-tick flush reached disk. Fixed by calling
  `pack_runtime.flush_storage()` before `build_manifest()` in
  `src/server/main.cpp`. **This error string has (at least) two unrelated
  root causes** — check both before assuming which one you've hit.

---

## 5. Dependency notes / unknowns

- **Cellulose** (`github.com/RechieKho/cellulose`) — evaluated as a greedy
  mesher, then reverted after it reproduced an NVIDIA-driver VAO/VBO
  heap-corruption crash (greedy-merged geometry blows past the renderer's
  buffer-reuse headroom far more often than the hand-rolled per-face
  mesher). Hand-rolled `vb::world::chunk_mesher` is the **permanent**
  meshing backend now, not a placeholder. `VB_WITH_MESHING` scaffolding was
  later removed outright at user request (2026-09-17). Full story in
  `state/changelog-part2.md`.
- **FastNoise2**: real upstream is `Auburn/FastNoise2` (the README's
  `electronicarts/fastnoise` link is a fork, don't use it). Pin is
  `v0.10.0-alpha`, not `v0.10.0` (that tag doesn't exist — a real,
  previously-unnoticed bug fixed in Phase 6.14, see `state/changelog-part1.md`).
- **GameNetworkingSockets** — resolved (Phase 1.2, 2026-09-11), the single
  hardest dependency in the project. Protobuf cannot be `FetchContent`-ed
  (must be a real package-manager install: vcpkg/apt/brew). GNS pinned to
  `v1.6.0` (not `v1.4.1`, which doesn't compile under a modern stdlib). ICE/
  WebRTC disabled. Windows crypto is BCrypt; Linux/macOS use system
  OpenSSL. Linked statically. **Full story, including local-verification
  toolchain quirks and the Homebrew double-`find_package` crash, is in
  `state/changelog-part1.md`'s Phase 1.2 entries — read those before
  touching `cmake/Dependencies.cmake`'s GNS block again.**
- **librg** is `zpl-c/librg`, pinned `v7.4.0`, wired in as the default
  entity-replication backend since 2026-09-17 (`VB_WITH_REPLICATION`
  default ON; the original hand-rolled linear scan remains as an explicit
  opt-out). Known limitation: chunk ids need `chunkamount.x*y*z` to fit
  signed int32 and each axis casts to `int16_t` — picked 1024 chunks/axis
  (±512×cell_size world units); an entity straying outside that range is
  silently excluded from interest until it re-enters. Full detail in
  `state/changelog-part2.md`.
- **Lua**: PUC-Lua has no upstream CMake; needs a wrapper `CMakeLists.txt`.
  sol2 is the chosen binding layer (pinned `v3.5.0`, not `v3.3.0` — that
  version's bundled optional impl fails under Clang ≥ 18).
- **raygui** is header-only (`raysan5/raygui`), needs exactly one TU with
  `#define RAYGUI_IMPLEMENTATION` in its own target (`vb_raygui_impl`) so
  project `-Werror` never touches vendored code — same pattern used for
  librg's `LIBRG_IMPL`.

---

## 6. Design decisions still open

Tracked in `ARCHITECTURE_SPEC.md` §18, repeated here for visibility:

1. ~~Lua binding layer: `sol2` vs. raw C API.~~ **Resolved: sol2.**
2. ~~Cellulose meshing API shape.~~ **Resolved and reverted** (2026-09-16)
   — hand-rolled meshing stays permanent.
3. librg version + whether we use its serialization or only its interest
   culling. **Resolved (2026-09-17): interest culling only**, own codec for
   payloads.
4. Chunk compression: LZ4 (spec's starting choice) vs. zstd vs.
   palette-only. Still open.
5. ~~World persistence / region file format.~~ **Resolved 2026-09-25:** flat
   per-region files (`vb::world::RegionStore`), not the previously-noted LMDB
   direction — reverted after review, see "Current status" above. Wired into
   `voxel_browser_server` only; `--singleplayer` still has no persistence.
6. Auth: `auth_mode = none | token` — handshake reserves the field, no
   service exists.

Other undecided:
- Test framework: doctest (in use) vs. Catch2 — never revisited since
  Phase 0's pick.
- Whether the client embeds the server for singleplayer as a library or
  spawns a child process — **currently: in-process library**
  (`Singleplayer` in `src/client/main.cpp` constructs `LoopbackNetwork`/
  `ServerSession`/`ClientSession` directly).
- ~~`WorldReplicator` streams a whole player's view box in one uncapped
  burst per connect, no per-tick pacing.~~ **Resolved 2026-09-30:**
  `WorldReplicator::set_send_budget_bytes(std::size_t)` caps `S2C_ChunkAdd`
  bytes sent to one player per `tick()` call (0 = unlimited, the default —
  GNS's 32 MiB send buffer from 2026-09-15 still covers the shipped default
  view distance without an operator opting in). A chunk that doesn't fit is
  simply left unset in the per-player revision map, so it's retried (and
  re-counted) on the next `tick()` — nothing is dropped, a large view box
  just spreads its initial burst over more ticks. The first frame of a tick
  always sends regardless of size, so one oversized chunk can't wedge a
  player forever. Wired to a new `server.toml` key
  `chunk_send_budget_bytes_per_tick` (`ServerConfig`, `src/server/main.cpp`).
  Distinct from `set_chunk_ingest_budget()`, which bounds the *ingest*
  (worldgen/disk-load) side only — this closes the *send* side that was
  still open after the 2026-09-25 ingest-budget fix.

---

## 7. Conventions to follow

- Namespaces `vb::<module>` (`vb::net`, `vb::world`, ...); headers under
  `inc/vb/<module>/`, mirrored by `src/<module>/`.
- No exceptions on hot paths; `Result<T,E>` for fallible ops. Never call
  `.error()` when a `Result` holds a value (union UB) — `REQUIRE(result)`
  then deref in tests, not `REQUIRE_MESSAGE(result, msg(err))`.
- Wire structs live in `inc/vb/protocol/`; every one gets a round-trip
  test and bumps `ENGINE_PROTOCOL_VERSION` + `docs/protocol.md` when
  changed. Concrete step-by-step for adding one: struct → `MessageType` →
  round-trip test → version bump → `docs/protocol.md` entry → integration
  test (see `CONTRIBUTING.md`).
- Keep both binaries buildable/runnable with `--headless` (CI +
  integration tests depend on it).
- Match `.clang-format`: tabs, no column limit, run clang-format before
  commit (CI `--Werror` on `src/**`).
- New Lua bindings should be **generic primitives, not game-specific
  ones** — game rules belong in `content/*` packs (e.g. `player:take()` is
  a generic inventory primitive; the actual crafting recipe list lives
  entirely in `content/base/crafting.lua`, not in engine code). See
  `CONTRIBUTING.md` for the worked example.
- Heavy deps (GNS, librg, Lua/sol2, FastNoise2, LZ4/xxHash) are declared in
  `Dependencies.cmake` but gated behind `VB_WITH_*` (several now default
  ON — check `CMakeLists.txt` for current defaults, don't assume OFF).
- doctest + MSVC STL: comparing/streaming a `std::string_view` in a
  `CHECK` needs `#include <ostream>` in that test TU (instantiates
  `toString<string_view>`).
- GCC gotcha: `uint64_t` (`unsigned long` on LP64) vs. `...ULL` literals
  trips `-Wsign-conversion` — always name wide constants `constexpr
  std::uint64_t`.
- Don't store `const BlockRegistry&` — callers pass `BlockRegistry::base()`
  temporaries; hold it by value.
- **xxHash/lz4 link order matters**: lz4's vendored source ships its own
  old `lib/xxhash.h` with no `XXH3_128bits`. `VB_WITH_COMPRESSION` links
  `xxHash::xxhash` **before** `LZ4::lz4` on purpose so the real header
  wins `-I` search order — don't reorder this.
- `std::vector<std::byte>` can't be built directly from
  `std::istreambuf_iterator<char>` — use the `read_whole_file()` helper in
  `assetsync/cache.cpp` instead of re-deriving it.
- **Clang: a nested class's `Foo() = default;` can't be used as a default
  argument value (`= {}`) on a member function of the *enclosing* class
  declared before that enclosing class is complete** — Clang errors
  wanting the nested class's field default-member-initializers "within
  definition of enclosing class" to compute the defaulted ctor's implicit
  exception spec, which isn't available yet at that point. Hit this in
  `inc/vb/world/lighting.hpp`'s `LightEngine::Neighbours() = default;`
  used as `relight_chunk(Chunk&, const Neighbours& = {})`'s default arg.
  Fix: give the nested ctor a user-provided empty body (`Neighbours() {}`)
  instead of `= default` — a user-provided ctor's noexcept-ness doesn't
  need the deferred computation, so no reordering/out-of-line-definition
  dance is required.

---

## Detail files

Every file below is full-detail material moved out of this core. Newest
work is summarized above under "Current status"; everything before that is
here, organized as it was originally written (mostly chronological,
newest-first within each file):

- **`state/changelog-part3.md`** — newest-first writeups moved out of
  "Current status" above: mob damage (and the still-open `PlayerHandle`
  corruption bug it surfaced), the hunger primitive, the "No PvP"
  confirmation, and back through all of Phase 7 (7.1-7.6), 6.18-6.21, and
  the visual/despawn/kind-rendering follow-ups, down to 6.18 (discrete
  punch combat) — see that file's own header for the full topic list;
  `changelog-recent.md` below picks up from there.
- **`state/changelog-recent.md`** — full writeups for the block-self-heal
  follow-up, Phase 6.18 (discrete punch combat), Phase 6.17 (decoupled
  block breaking + `MovementBindings`), Phase 1.3 networking polish
  (two-process smoke test, per-IP connection cap, hostname resolution),
  Phase 6.15 (kitchen-sink example pack), Phase 6.14 (Lua-driven worldgen/
  FastNoise2), Phase 6.13 (read-only server config visibility) — these
  phases were never logged into the older §8 structure below and are not
  duplicated in the next two files.
- **`state/changelog-part1.md`** — Phase 6.1/6.4/6.5/6.6/6.7/6.8/6.9/6.11/
  6.16 landed-feature writeups; the 2026-09-15 investigations (NVIDIA
  driver VAO/VBO heap corruption, FPS dip during chunk streaming,
  cross-chunk sky-light band, near-black flash on predicted breaks, chunk
  meshing moved off the main thread, GNS send-buffer overflow root cause);
  the 2026-09-10/11 Phase 0-4 bootstrap entries (config, transport,
  handshake, session layer, physics/netcode, worldgen, entity visuals,
  block editing, Lua VM); "Gotchas learned this pass".
- **`state/changelog-part2.md`** — Phase 5.1 `content/base` pack + the gap
  it found (neither binary loaded a pack at all before this); Phase 5.2-5.5
  (main menu, chat, player list/join-leave, day/night, death/respawn, sfx
  docs, real inventory sync + hotbar, hold-to-break progress, dropped-item
  entity, crafting recipes, `--singleplayer` running the real content pack,
  documentation pass); the Cellulose meshing spike and revert; librg wired
  in as the interest backend and flipped to default; Phase 6 design-only
  passes (entity classes, UI immediate-mode, keybind channel, `vb.db`,
  block-damage breaking, worldgen biome-selection redesign).
- **`state/gotchas.md`** — long-form compiler/toolchain traps referenced
  from §4 above (the `std::erase` MSVC-STL bug, the MSVC ternary
  ambiguity, the two independent asset-sync hash-mismatch bugs, the
  stale-comment lesson, the clang-format-untrustworthy-locally note).

Anything **not** listed above and not inline in this file no longer exists
in `STATE.md`'s history — if you're looking for something and can't find
it here, check `git log -- STATE.md` for when it might have been removed,
or `REMAINING_TASKS.md`/`ARCHITECTURE_SPEC.md` for design-level context.
