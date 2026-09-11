# STATE — Working Notes for Future Agents

> Living scratchpad of gotchas, landmines, open decisions, and small TODOs
> discovered while working in this repo. **Update this file** when you learn
> something non-obvious or fix something listed here.
>
> Companion docs: `ARCHITECTURE_SPEC.md` (target design) · `REMAINING_TASKS.md`
> (implementation backlog). This file is for *traps and context*, not the plan.

Last updated: 2026-09-11 (fixed spawn position — was embedded in terrain for ~half of all seeds)

---

## 0. Repo snapshot

- `674d88f Initial commit` + the Phase 0 restructure (uncommitted at time of
  writing). Still **no tags**. Remote: `https://github.com/RechieKho/voxel_browser.git`.
- Source tree now matches spec §4: `vb_core` (`src/core/`), `vb_render`
  (`src/render/`), `voxel_browser` (`src/client/`), `voxel_browser_server`
  (`src/server/`), `vb_tests` (`tests/`). Other module dirs are `.gitkeep` stubs.
- Builds green on Windows/clang with `-DVB_WARNINGS_AS_ERRORS=ON`; all 3 ctest
  cases pass (`vb_tests`, `server_smoke`, `client_smoke`).
- `ARCHITECTURE_SPEC.md` / `REMAINING_TASKS.md` are still design intent for
  Phase 1+.

---

## 1. Build-blocking bugs — ✅ ALL FIXED in Phase 0

### 1.1 bare `PROJECT_VERSION` in `main.cpp` — FIXED
`src/main.cpp` is gone. Version info is now a generated header
`vb/core/version.hpp` (from `cmake/version.hpp.in`, written to
`${CMAKE_BINARY_DIR}/generated/include`), exposing `vb::kVersionString`,
`vb::kEngineProtocolVersion`, etc. `vb::core::describe_build()` formats the banner.

### 1.2 raylib not pinned — FIXED
`cmake/Dependencies.cmake` uses `GIT_TAG 5.5 GIT_SHALLOW TRUE`. **Note: 5.5, not
6.0** — 6.0 was not a real upstream tag at the time; README/spec prose still say
"6.0". raygui pinned to matching `4.0`. Bump both together.

### 1.3 `cmake_minimum_required` — FIXED
Now `VERSION 3.25` (matches the `block()` usage and README).

### 1.4 CMake ≥ 4.0 rejects doctest 2.4.11 — worked around
doctest 2.4.11 declares `cmake_minimum_required(VERSION 3.0)`, which CMake 4.x
errors on. `cmake/Dependencies.cmake` sets `CMAKE_POLICY_VERSION_MINIMUM 3.5`
when `CMAKE_VERSION >= 4.0`. GitHub runners are still on CMake 3.3x so CI never
hit it. Remove the shim when doctest is bumped past a release that fixes this.

---

## 2. Naming — ✅ RESOLVED

`PROJECT_NAME` is now **`voxel_browser`**. Executables: `voxel_browser` (client)
and `voxel_browser_server`. CI artifacts are
`voxel_browser-<target>-<arch>-<build_type>` and each now contains a directory
with *both* binaries (bundle/publish still merge by `voxel_browser-*` pattern).

---

## 3. CI landmines

- **No tags exist.** `setup_metadata.yml` runs `git describe --tags --abbrev=0`
  for the `version` output — this **errors** with no tags in history. `bundle`
  and `publish` depend on `setup_metadata`. First `git tag v0.0.1` will unblock;
  until then anything past `build_*` is untested / likely red.
- `publish.yml` triggers only on `v*.*.*` tags and pulls artifacts from
  `runner.yml` by name `${project_name}` (note trailing space in the YAML on
  `name:` line 26 — `action-download-artifact` may or may not trim it).
- `lint.yml` installs clang-format via `pip install` then runs it via
  `pipx run clang-format` (two different mechanisms; the pip install is dead
  weight). Lints `src/**` with `--Werror` — every new file under `src/` must be
  clang-format-clean or CI fails.
- Build workflows use `actions/checkout@v4` with `submodules: recursive`, but
  **there are no submodules** — deps are `FetchContent`. Harmless now; README's
  "pull submodules" / `git clone --recursive` language is misleading, deps are
  fetched at configure time.
- `build_linux.yml` installs X11/GL dev packages for raylib, plus (since Phase
  1.2) `libssl-dev libprotobuf-dev protobuf-compiler` for `VB_WITH_NET`.
- `runner.yml` fires on push to `main` and `*-workflow` branches, plus PRs.
- **`VB_WITH_NET` CI coverage is uneven — check before assuming it's tested
  everywhere:** Linux (apt) and Windows (vcpkg, pre-installed on
  `windows-latest`) build it; **macOS does not** — see §5's GNS entry for why
  (single-arch Homebrew protobuf vs. the universal arm64+x86_64 build). If
  macOS ever gets it, `build_macos.yml`'s configure step is where to add it.
- **Never call `find_package(Protobuf REQUIRED)` a second time anywhere in the
  tree.** GameNetworkingSockets' own `src/CMakeLists.txt` already calls it;
  calling it again (even just as a "fail fast with a clearer message"
  convenience, which is what caused this) fatal-errors on some protobuf
  installs — hit on Homebrew's, not on vcpkg's — with "Some (but not all)
  targets in this export set were already defined." Fixed 2026-09-11 (§8) by
  deleting the redundant call; don't reintroduce it.

---

## 4. Config / style quirks

- `.clang-format` is **Godot's** clang-format file, verbatim — includes
  `Language: Java` blocks with `JavaImportGroups: ['org.godotengine', ...]` and
  Godot TODO comments. Cosmetic; the C++ rules are what matter: **tabs**
  (`UseTab: Always`, `TabWidth 4`), `ColumnLimit: 0` (no auto-wrap),
  `AccessModifierOffset: -4`, pointers right-aligned, `Cpp11BracedListStyle: false`.
- `.clang-format` sets `Standard: c++17`; CMake sets `CMAKE_CXX_STANDARD 20`.
  Formatter parses as C++17 — fine unless C++20-only syntax confuses it; bump to
  `c++20` if that happens.
- `.gitignore` ignores `build`, `*.local`, `compile_commands.json`,
  `.vscode/*` (except `extensions.json`). `CMAKE_EXPORT_COMPILE_COMMANDS ON` is
  set — symlink/copy `build/compile_commands.json` to root for tooling.
- `CMakeLists.txt:54` uses `file(GLOB_RECURSE SOURCE_FILES ...)` — the usual
  CMake caveat: adding a `.cpp` doesn't trigger reconfigure. Either keep
  `cmake` re-runs in the loop or switch to explicit source lists in Phase 0.
- `libfantastic` (the lib target) is `add_library(... INTERFACE)` — assumes
  header-only. The Phase 0 split makes `vb_core` a real STATIC lib; don't carry
  the INTERFACE assumption forward.

---

## 5. Dependency notes / unknowns

- **Cellulose** (`github.com/RechieKho/cellulose`) is the maintainer's own repo.
  API is unknown — **inspect it directly** before writing any meshing code
  (`REMAINING_TASKS.md` Phase 2 spike). Does it emit vertex data or own GPU
  buffers? What chunk size does it expect? The 32³ choice in the spec is an
  assumption pending this.
- **FastNoise2**: README links `electronicarts/fastnoise`. Upstream is
  `Auburn/FastNoise2`; the EA repo is a fork. Confirm which one, and that it
  ships a usable `CMakeLists` / `FastNoise2Config.cmake` (it does export a
  target, but SIMD level detection can be fiddly on CI).
- **GameNetworkingSockets** — **resolved (Phase 1.2, 2026-09-11), was the
  single hardest dependency in the project.** Protobuf cannot be
  `FetchContent`-ed (its CMake config is only generated by an `install()`
  step, and the library doesn't exist yet at FetchContent's configure time) —
  it must be a real package-manager install: vcpkg (Windows), apt (Linux),
  brew (macOS, but not yet wired into CI — see §3). GNS pin is `v1.6.0`, not
  `v1.4.1` — the older tag doesn't compile under a modern stdlib
  (`std::string_view::c_str()`, not a real API). ICE/WebRTC disabled
  (`ENABLE_ICE=OFF`), which also lets `GIT_SUBMODULES` skip the huge `webrtc`
  submodule — only `abseil` + `vjson` are needed (note: v1.4.x used `picojson`
  for the same purpose; v1.6.0 renamed it to `vjson` — check `.gitmodules`
  again if the pin ever moves). Windows crypto is BCrypt (no OpenSSL needed
  there); Linux/macOS use system OpenSSL. Linked statically
  (`GameNetworkingSockets::static`) like everything else here. Full story,
  including the local-verification toolchain quirks and the Homebrew
  double-`find_package` crash, is in §8's Phase 1.2 entries — read those
  before touching `cmake/Dependencies.cmake`'s GNS block again.
- **librg** is `zpl-c/librg` — single-header, v0.x, API has churned across
  versions. Pin a specific commit and write down which (`docs/protocol.md`).
- **Lua**: PUC-Lua has no upstream CMake; needs a wrapper `CMakeLists.txt`.
  sol2 vs. raw C API is `ARCHITECTURE_SPEC.md` open question #1 — undecided.
- **raygui** is header-only (`raysan5/raygui`), needs exactly one TU with
  `#define RAYGUI_IMPLEMENTATION`. Ships with raylib's org; version-match to
  the raylib tag chosen in 1.2.

---

## 6. Design decisions still open

Tracked in `ARCHITECTURE_SPEC.md` §19, repeated here for visibility:

1. Lua binding layer: `sol2` vs. raw C API.
2. Cellulose meshing API shape (blocking spike, see §5).
3. librg version + whether we use its serialization or only its interest culling.
4. Chunk compression: LZ4 (spec's starting choice) vs. zstd vs. palette-only.
5. World persistence / region file format — deferred, but don't design the chunk
   store as in-memory-forever.
6. Auth: `auth_mode = none | token` — handshake reserves the field, no service.

Other undecided-but-not-yet-in-spec:
- Test framework: doctest vs. Catch2 (`REMAINING_TASKS.md` Phase 0 lists both).
- Final project name (§2 above).
- Whether the client embeds the server for singleplayer as a library or spawns a
  child process (spec says in-process library).

---

## 7. Conventions to follow (once code exists)

- Namespaces `vb::<module>` (`vb::net`, `vb::world`, ...); headers under
  `inc/vb/<module>/`, mirrored by `src/<module>/`.
- No exceptions on hot paths; `Result<T,E>` for fallible ops (spec §1/§14).
- Wire structs live in `inc/vb/protocol/`; every one gets a round-trip test and
  bumps `ENGINE_PROTOCOL_VERSION` + `docs/protocol.md` when changed.
- Keep both binaries buildable/runnable with `--headless` (CI + integration
  tests depend on it).
- Match `.clang-format`: tabs, no column limit, run clang-format before commit
  (CI `--Werror` on `src/**`).

---

## 8. Done / resolved

_(Move items here with a date + commit when fixed, so the history is visible.)_

- **2026-09-10 — Phase 0 restructure** (uncommitted). Modular CMake
  (`vb_core`/`vb_render`/`voxel_browser`/`voxel_browser_server`/`vb_tests`),
  `cmake/{Dependencies,Warnings,Sanitizers}.cmake` + `cmake/version.hpp.in` +
  `cmake/lua/CMakeLists.txt`, runnable client/server skeletons with `--headless`,
  generated version header, CI updated (multi-binary staging, ctest on all 3 OSes,
  `upload-artifact` v7→v4 typo, lint via `find`, `git describe` no-tag fallback),
  `docs/{protocol,lua-api}.md` stubs, `.clang-format` → c++20, `PROJECT_NAME` →
  `voxel_browser`. Build-blocking bugs §1.1–1.3 fixed; §1.4 worked around.
  Decisions locked: sol2 (§19 Q1), doctest, LZ4, explicit source lists.

- **2026-09-10 — Phase 1.1/1.2/1.3 (partial)** (uncommitted). `vb_core`:
  `result.hpp` (`Result<T,E>`/`Status`), `error.hpp` (`CoreError`/`ProtocolError`
  + total `message()` switches), `math.hpp`, `ids.hpp`, `log.{hpp,cpp}`.
  `vb/protocol/`: `byte_buffer.hpp` (`ByteReader`/`ByteWriter` + LEB128 varints,
  error-accumulating), `message.hpp` (envelope, `MessageType`, `lane_for`),
  `handshake.hpp`+`handshake.cpp` (7 handshake message structs, encode/decode).
  18 unit test cases / 113 assertions, all green under `-Werror`. `docs/protocol.md`
  filled in. **Still open in Phase 1:** TOML config loader, GNS transport,
  connection FSMs, librg spike, client shell (camera/overlay).

- **2026-09-10 — Phase 1.2/1.3 transport + handshake** (uncommitted).
  `vb/net/`: `transport.hpp` (`Transport` interface, `TransportEvent`,
  `lane_for`/`send_mode_for_lane`, `send_message` helper), `loopback.{hpp,cpp}`
  (`LoopbackNetwork` in-process backend), `handshake.{hpp,cpp}` (`ServerHandshake`
  + `ClientHandshake` FSMs with host callback hooks). `NetError` enum added to
  `core/error.hpp`. `tests/unit/net_test.cpp` drives the whole handshake over
  loopback (26 cases / 185 assertions total, green under `-Werror`).
- **2026-09-10 — Phase 1 session layer + integrated singleplayer** (uncommitted).
  `vb/net/session.{hpp,cpp}`: `ServerSession` (per-conn handshake driver,
  timeouts, monotonic net-id alloc, `take_joins`/`take_leaves`), `ClientSession`.
  `vb/net/integrated.{hpp,cpp}`: `IntegratedGame` — loopback server+client in one
  object. `voxel_browser --singleplayer` runs it and completes the join
  (headless too); `singleplayer_smoke` CTest. `ServerHandshake::grant()` exposes
  the `JoinGrant`. `IntegratedGame` needs ~4 `tick()`s to settle (server-polls-
  then-client-polls each tick = one message hop per tick).
  **Next:** `GnsTransport` behind `VB_WITH_NET`.

- **2026-09-11 — Fixed spawn Y: was embedded in terrain by construction for
  ~half of all seeds** (uncommitted). Follow-up report after the fall-through
  fix below: "the player stuck inside blocks on spawn" -- **no falling
  involved this time**, a separate bug. Root cause: `JoinGrant::spawn_pos`
  defaults to a fixed `{0, 64, 0}` (`inc/vb/net/handshake.hpp`), and neither
  `--singleplayer` nor the dedicated server ever overrode it with a real
  terrain height -- singleplayer's hardcoded seed 7 happens to have
  `surface_height(0,0) == 56` (safely below 64, purely by luck), but the
  standalone server now picks a **random** seed by default
  (`server/main.cpp`'s `random_seed()`), and `base_height=64` /
  `amplitude=28` means the real surface ranges roughly `[36, 92]` — probed 30
  seeds, **19 of 30 (63%)** had `surface_height(0,0) >= 64`, i.e. the fixed
  spawn point was at or below ground. Seed 1 is dramatic: surface height 78,
  fourteen blocks above where the player would spawn — fully entombed in
  stone from the instant they joined, zero fall.
  Fixed by adding `worldgen::default_spawn_position(generator, x, z)`
  (`vb/worldgen/generator.{hpp,cpp}`) — feet one voxel above the real
  `surface_height` at the spawn column — and wiring it into a
  `HandshakeServerHost::on_ready` in both `--singleplayer` (`sp_server_host()`
  in client `main.cpp`) and the dedicated server (`server/main.cpp`), neither
  of which supplied a custom host before. `ServerSession`'s constructor
  already fills in `net_id`/`world_seed` on top of whatever `on_ready`
  returns (existing wrapper logic, unchanged) — the host only needs to set
  `spawn_pos`. Tests: `tests/unit/worldgen_test.cpp` — asserts the computed
  spawn sits exactly one voxel above `surface_height` for 3 seeds (including
  1, the dramatic case) and for a non-origin spawn column.
  **This is a different bug from the one below and doesn't supersede it** —
  a correct spawn *point* still needs the *chunk at that point* to be loaded
  before physics starts touching it; both fixes matter together. Not
  discovered together because singleplayer's fixed seed 7 masked this one
  during all of Phase 3-5's development and testing.

- **2026-09-11 — Join-time fall-through-world / embedding bug fixed**
  (`69ea41c`). Reported: "when the player joins, the player immediately
  falls outside the world first, which leads to player get embedded inside
  the terrain." Root cause: physics is entirely input-driven
  (`ServerSession::handle_input_batch` only runs `step_movement` when a
  `C2SInputBatch` arrives) and starts the instant the client sends its first
  input, which can easily be *before* the spawn chunk's async worldgen has
  finished (`WorldGenWorkerPool` runs on real background threads outside
  `kSynchronous` mode — `--singleplayer`'s pool included). An unloaded chunk's
  `solid_at`/`has_chunk` reads as plain air, so the player free-falls with
  **zero collision** for however many ticks generation takes; swept collision
  only ever prevents *new* penetration during a move, it never resolves a
  pre-existing one, so once the chunk finally loads with the player's Y
  already below the real surface, they're just stuck inside solid terrain —
  nothing ever pushes them back out.
  Fix: `physics::ground_area_loaded(feet, has_chunk)` (new, header-only
  template in `movement.hpp` so it works against both `World::has_chunk` and
  `ClientChunkStore::has` without a shared interface) checks the player's own
  chunk plus two below it; `ServerSession::handle_input_batch` and
  `ClientSession::push_input` both skip `step_movement` entirely (freezing
  position/velocity, not just zeroing gravity) whenever it's false. The
  server's freeze is what actually matters for correctness (it's
  authoritative); the client-side one is belt-and-suspenders so the local
  camera doesn't show a premature fall before the first snapshot arrives —
  verified this doesn't fight `reconcile()` (unguarded on purpose: replaying
  unacked inputs against *current*, likely-by-then-loaded chunk data is
  correct) by working through why `netcode_test.cpp`'s fly-mode tests (no
  `WorldReplicator` attached at all, so `chunks_` never receives any chunks)
  still pass: `reconcile()` snaps `predicted_` straight to the server's
  authoritative position every tick regardless of what `push_input` did
  locally, so the client-side freeze only ever affects the sub-tick gap
  before the next snapshot, never overall convergence.
  Tests: `tests/unit/physics_test.cpp` — 5 direct cases against
  `ground_area_loaded()` (nothing loaded, own chunk only, own+2 below, one
  gap in the middle, negative-Y `floor_div` correctness).
  **Known test-coverage gap, stated plainly:** there is *no* automated
  integration test reproducing the actual async join race end-to-end.
  `WorldGenWorkerPool::kSynchronous` (used by most existing integration
  tests) generates instantly inside `submit()`, so it structurally cannot
  exhibit the gap this bug lived in; the real async pool's timing isn't
  controllable enough for a deterministic CI test without adding test-only
  hooks, which felt like over-engineering for this fix. Confidence rests on
  the unit-tested decision function + code-inspection of the two call sites,
  not an end-to-end repro — say so if a future change to the join sequence
  needs that confirmed differently.

- **2026-09-11 — Chunk-neighbour remesh bug fixed** (`c3393be`). Reported by
  the user: "very minor AO error, perhaps arises from receiving data from the
  server" + "square chunks visible under water." Root cause:
  `ChunkRenderer::sync()` only re-`mesh_chunk()`s a chunk when
  `chunk->revision()` changes (chunk_renderer.cpp) — `mesh_chunk()` itself is
  a pure function of current store state and was always correct, but **three
  of the four `ClientChunkStore` mutators never bumped a *neighbour's*
  revision**, only `edit_block()` (the client's own optimistic edits) did:
  - `apply_add` — a chunk arriving after an already-loaded neighbour left
    that neighbour's border permanently meshed as if this chunk were still
    unloaded (air) — wrong culling *and* wrong AO along that seam, forever,
    unless something else happened to touch the neighbour later. Order-
    dependent on server streaming order — explains the "square chunks" report
    exactly (water chunks are flat/homogeneous, so an unculled border face is
    a very visible full 32×32 quad) and is entirely about *timing of data
    arriving from the server*, matching the other half of the report too.
  - `apply_delta` — the authoritative block-edit broadcast (anyone's edit,
    not just yours) never bumped the neighbour whose culling/AO depends on
    a border voxel it just changed.
  - `apply_remove` — a chunk unloading never told the neighbour that had been
    culling a face against it to re-expose that face.
  Fixed by extracting `bump_all_neighbor_revisions(coord)` (bump all 6
  face-adjacent loaded neighbours, unconditionally) and calling it from all
  three; `edit_block()` keeps its own tighter border-only variant since it
  only ever touches one voxel and knows exactly which single neighbour (if
  any) is affected. Regression tests in `tests/unit/mesher_test.cpp` assert
  the neighbour's revision actually changes (not just that `mesh_chunk()`'s
  output would be correct if called again — the bug was entirely about
  *whether* it gets called again) — verified they fail without the fix
  (reverted the source, reran, confirmed all 3 fail with `1 > 1`) before
  trusting them.
  **Lesson for next time a chunk-border bug shows up:** any code path that
  changes what a chunk looks like from the outside (new data, an edit
  anywhere in it, unloading) needs to bump *every loaded neighbour's*
  revision, not just its own — the renderer has no other signal to re-mesh a
  chunk whose own data didn't change but whose correct mesh output did.

- **2026-09-11 — macOS Homebrew protobuf configure crash fixed** (`f86aeec`).
  Reported by the user building `-DVB_WITH_NET=ON` locally on macOS with
  `brew install protobuf`:
  ```
  CMake Error at .../protobuf-targets.cmake:42 (message):
    Some (but not all) targets in this export set were already defined.
    Targets Defined: protobuf::libprotobuf-lite, protobuf::libprotobuf, ...
    Targets not yet defined: protobuf::libupb, protobuf::protoc-gen-upb, ...
  ```
  Root cause: `cmake/Dependencies.cmake` called `find_package(Protobuf
  REQUIRED)` itself (added purely so a missing protobuf would fail with a
  clearer message) *and then* `FetchContent_MakeAvailable(gamenetworkingsockets)`
  triggered GNS's own `src/CMakeLists.txt:12 find_package(Protobuf REQUIRED)`
  — two `find_package(Protobuf)` calls in one configure run. Homebrew's
  generated `protobuf-targets.cmake` has a fatal-error guard against exactly
  this (the two calls end up requesting slightly different component sets, so
  the second sees a partially-already-defined target list and refuses rather
  than silently re-defining). vcpkg's protobuf on Windows didn't trip the same
  guard, which is why this wasn't caught during Phase 1.2's own local
  verification (Windows-only, see below) — **a dependency behaving on one
  platform's package manager doesn't mean it behaves on another's; the
  `VB_WITH_NET` local verification story only ever covered Windows before this
  bug report.** Fix: deleted our own `find_package(Protobuf REQUIRED)` call
  entirely; GNS's internal one is now the sole call and already produces a
  clear "Could NOT find Protobuf" error on its own if it's missing, so nothing
  was lost. **Do not add `find_package(Protobuf ...)` back to
  `Dependencies.cmake`** — see §3 and §5 above.
  Verified only that the *configure* step now succeeds on Windows with the fix
  in place (reconfigured with the vcpkg toolchain file, same as Phase 1.2's
  original verification) — the macOS report itself is unconfirmed-fixed
  pending the user re-running it; if it still fails on macOS after this, the
  next suspect is the OpenSSL linkage (`brew install openssl` — macOS uses
  GNS's default OpenSSL crypto backend, not BCrypt), not protobuf.

- **2026-09-11 — Phase 1.2: `GnsTransport`, real UDP networking** (`cc2241a`).
  `vb/net/gns_transport.{hpp,cpp}` — a real `Transport` backend over
  GameNetworkingSockets, same "always-present header, `kBackendUnavailable`
  stub without the flag" pattern as `vb::script::Vm`. One process-wide
  `GnsRuntime` (refcounted `GameNetworkingSockets_Init`/`_Kill`; GNS exposes
  exactly ONE global connection-status callback for the whole process, not
  one per interface) routes each status-change event to the owning
  `GnsTransport` via listen-socket/connection-handle registries — needed so a
  server + several clients can share one process (tests) without cross-talk.
  `voxel_browser_server` now actually runs a game (`World` +
  `WorldGenWorkerPool` + `ServerSession` + `WorldReplicator` over a real
  listen socket) instead of an empty sleep loop. `voxel_browser`'s
  non-singleplayer path connects for real (`RemoteConnection` in `main.cpp`);
  `main.cpp` was refactored to drive singleplayer and remote play through one
  `ClientSession*` instead of duplicating the per-frame logic.
  `tests/unit/gns_transport_test.cpp`: real UDP connect/send/disconnect over
  127.0.0.1 (`#if VB_WITH_NET`, mirrors the `vb::script::Vm` test-gating style).

  **This was the single hardest dependency in the project so far — details
  matter if it needs touching again:**
  - **Protobuf cannot be `FetchContent`-ed for this.** GameNetworkingSockets'
    CMakeLists does a bare `find_package(Protobuf REQUIRED)`. Protobuf's own
    CMake only *generates* a discoverable config (`protobuf-config.cmake`) as
    part of an `install()` step, and its library doesn't exist yet at
    FetchContent's configure time either way — there is no clean way to
    satisfy `find_package(Protobuf)` from a bare source checkout. Protobuf
    must come from a real package manager: **vcpkg on Windows**
    (`vcpkg install protobuf:x64-windows` +
    `-DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake` — this
    box now has vcpkg cloned+bootstrapped at `D:\vcpkg`), **apt on Linux**
    (`libprotobuf-dev protobuf-compiler`), **brew on macOS** (`protobuf`).
    `cmake/Dependencies.cmake` has the full write-up.
  - **sol2's pattern does not repeat here**: unlike sol2 (bump the pin, done),
    this is a "the dependency's own build system requires a pre-installed
    sub-dependency" problem — no version bump fixes it.
  - **GNS pin bumped v1.4.1 → v1.6.0**: v1.4.1 doesn't compile at all under a
    strict/modern stdlib — `std::string_view::c_str()` (not a real API) in
    `csteamnetworkingsockets.cpp` and two other files. Fixed upstream by v1.6.0.
  - **ICE/WebRTC disabled** (`ENABLE_ICE=OFF`, `USE_STEAMWEBRTC=OFF`) — we only
    ever dial a known dedicated-server address, never P2P/NAT-punched. Also
    restricted `GIT_SUBMODULES` on the FetchContent_Declare to skip the
    multi-hundred-MB `webrtc` submodule (**note**: v1.6.0 renamed the other
    required submodule from `picojson` to `vjson` — check `.gitmodules` again
    if the pin moves).
  - **Crypto backend**: `USE_CRYPTO=BCrypt` on Windows (built into the OS, no
    extra dependency); Linux/macOS use GNS's default, system OpenSSL
    (`libssl-dev` / `brew install openssl`).
  - **Link statically** (`GameNetworkingSockets::static`, `BUILD_SHARED_LIB
    OFF`) — every other dependency here is static/header-only; a shared GNS
    lib would need copying next to the .exe on Windows (no RPATH equivalent).
  - **GNS's public headers need the SYSTEM-include treatment**: same fix as
    doctest/raygui/toml++ (`get_target_property(... INTERFACE_INCLUDE_DIRECTORIES)`
    + re-add `SYSTEM`) — its headers trip `-Wold-style-cast`/`-Wsign-conversion`/
    `-Wlanguage-extension-token` under our `-Werror` set. Done in
    `src/core/CMakeLists.txt`.
  - **macOS CI does NOT build `VB_WITH_NET` yet** — it's a universal
    (arm64+x86_64) build, and a brew-installed protobuf is single-arch, which
    breaks linking the slice that doesn't match the runner's host arch. Needs
    a universal protobuf (vcpkg triplet, or building protobuf from source for
    both arches) before enabling.
  - **`connect()` only accepts numeric IP literals** — `SteamNetworkingIPAddr::
    ParseString()` doesn't resolve DNS. "localhost" / hostnames need
    `getaddrinfo` added to `GnsTransport::connect`; not done.
  - **Local-box-only linker quirk (not a CI concern)**: plain `clang++.exe`
    (GNU driver) targeting the MSVC ABI makes CMake's `MSVC` variable true, so
    GNS's *vendored* abseil submodule (still built even with ICE off — some
    non-ICE code apparently uses it) applies MSVC-style linker flags
    (`-ignore:4221`) that only a Microsoft-syntax-aware driver understands.
    Plain `clang++` rejects it ("unknown argument"); **`clang-cl.exe`** (same
    LLVM install, MSVC-compatible driver) accepts it fine. Real CI is
    unaffected — Windows CI uses actual `cl.exe` via `msvc-dev-cmd`,
    Linux/macOS CI never sets `MSVC` true. Local verification builds on this
    box needing `VB_WITH_NET` should use `clang-cl`/`clang-cl++`, not
    `clang`/`clang++`.
  - Full local validation trail: GNS built standalone under both plain
    `clang++` (compiles; only the unrelated abseil link flag fails) and is
    expected clean under `clang-cl` (build was in flight when this was
    written — check `voxel_browser`/`voxel_browser_server`/`vb_tests` actually
    linked before trusting this note blindly next time).
  - **Smoke-test regression caught and fixed**: `server_smoke`/`client_smoke`
    (default build, `VB_WITH_NET` off) broke the moment the binaries started
    actually calling `listen()`/`connect()` instead of being no-ops. Fixed by
    (a) making the server treat `kBackendUnavailable` specifically as
    non-fatal (see above) and (b) flipping `client_smoke`'s CTest expectation
    — with no `--singleplayer`/`--server` override it now legitimately tries
    to reach `127.0.0.1:27015` with nothing listening, so the smoke test
    became "fails gracefully with a clear message," not "succeeds";
    `PASS_REGULAR_EXPRESSION` alone (not `WILL_FAIL` — the two combine by
    inverting an already-passing regex match into a reported failure, don't
    stack them) asserts that. Lesson: wiring a previously-inert code path for
    real changes what "the binary just starts up" tests actually exercise —
    re-check smoke tests whenever a stub becomes real.

- **2026-09-11 — Entity billboard-sprite placeholder implemented** (Phase 3.5,
  `7f64581`). `inc/vb/render/entity_visual.hpp` (header-only, no raylib,
  unit-tested like `camera.hpp`): `resolve_anim_clip` (priority state machine),
  `bearing_degrees`/`direction_bucket`/`select_pose` (direction math, mirroring
  for the far half of the sectors), `DirectionBucketTracker` (flicker
  hysteresis), `EntityPresentationState` (per-entity orchestration + clip-time
  tracking). `vb/render/entity_renderer.{hpp,cpp}`: raylib-backed, pImpl (no
  raylib types in the header, matching `ChunkRenderer`); one `DrawBillboardPro`
  per tracked entity against a 1×1 white texture tinted by a deterministic
  `NetId` hash (stand-in art, so simultaneous entities are at least visually
  distinguishable). Wired into `main.cpp` next to `chunk_renderer`, same
  headless guard, overlay now shows an `entities N` count.
  `tests/unit/entity_visual_test.cpp` — 14 cases. 113/119 total (non-Lua/Lua
  builds) test cases green.
  - **Gotcha:** `resolve_anim_clip`'s horizontal-speed calc needs both
    operands explicitly cast to `double` before multiplying
    (`static_cast<double>(vel.x) * vel.x` still promotes the second operand
    implicitly) — `-Wdouble-promotion` catches this, same class of trap noted
    for Phase 1.5's camera code.
  - **Confirmed, not assumed:** raylib 5.5's `DrawBillboardPro` with a fixed
    `up = {0,1,0}` gives exactly Y-axis (cylindrical) billboarding for free —
    read `rmodels.c` directly rather than trusting the header comment; its
    `right` vector comes from the camera view matrix but is always horizontal
    regardless of pitch (cross of any forward vector with world-up has zero Y
    component).
  - **Not yet visually verified**: no second connected player exists to look
    at (needs `GnsTransport`, or an in-process 2-client test harness with a
    window) — correctness rests on the unit tests + code review, not eyes on
    screen. Flag if a real two-player session shows different behaviour than
    the math predicts.
  - **Still open:** `SpriteVisual` ECS component (waits on 4.2 having a real
    per-kind visual def to populate it with); the `dead`/`hurt_pulse`/`acting`
    flag bits are defined but nothing sends them yet (server only ever sets
    bit 0).

- **2026-09-11 — Entity visual presentation decided: billboard sprites**
  (committed `adfc68e`).
  (design only, no code). Players/entities are Don't Starve-style 2D
  billboards, not 3D blocky models — `ARCHITECTURE_SPEC.md` §11.3, open
  question §19 Q7, backlog `REMAINING_TASKS.md` Phase 3.5. Closes a real,
  previously-silent gap: nothing has ever drawn a remote player client-side —
  `remote_entities()`/`interpolated_pos()` (Phase 3.4) are correct but nothing
  visible used them. Key technical finding baked into the design: raylib's
  `DrawBillboardPro(..., up={0,1,0}, ...)` gives exactly the wanted Y-axis
  (cylindrical) billboard for free — checked directly against `rmodels.c`
  (5.5): its `right` vector comes from the camera view matrix but is always
  horizontal regardless of pitch (cross of any forward vector with world-up has
  zero Y component), so no custom quad math is needed. Plan: Phase 3 ships a
  hardcoded single-frame placeholder (no Lua/pack dependency, mirrors how
  Phase 2 shipped a mesher ahead of Cellulose); real art + the
  `vb.register_entity{ visual = {...} }` surface land with 4.2/4.4/5.1.

- **2026-09-11 — Bugfixes from the first manual smoke test** (committed `009195e`).
  Three real bugs found by testing on Windows + macOS:
  1. **`sol::nil` doesn't exist on Apple platforms** — sol2 disables its `nil`
     alias by default whenever `__MAC_OS_X_VERSION_MAX_ALLOWED`/`__OBJC__`/a
     `nil` macro is visible (avoids clashing with Objective-C `nil`); see
     `sol/version.hpp` `SOL_NIL_I_`. Fixed by using `sol::lua_nil` (the
     always-defined underlying constant) instead of `sol::nil` in `vm.cpp`.
     **Lesson: never use `sol::nil`/`sol::type` names that shadow platform
     macros — prefer the `lua_`-prefixed sol2 spellings.**
  2. **Sprint had no effect (and walking was already ~17% under `walk_speed`)**
     — `step_movement` applied ground friction *every* tick regardless of
     input, fighting the acceleration step every frame. The fixed point of
     that tug-of-war is `accel/friction` (≈3.75 m/s with the shipped
     constants) **independent of the wish speed**, so both walk (4.5) and
     sprint (7.0) converged to the same capped speed. Fixed: friction now only
     applies when there's no active wish direction (skid-to-a-stop on
     release); acceleration alone drives velocity toward `wish_vel` while
     moving. Regression tests added (`physics_test.cpp`): sustained-movement
     reaches `walk_speed`/`sprint_speed`, and releasing input decelerates via
     friction. **Lesson: an FPS-style accel/friction model needs friction
     gated on "no input", not unconditional — the two tests that existed
     before only checked qualitative behaviour (stops at a wall, settles on a
     floor), never a steady-state speed value, so this shipped unnoticed.**
  3. **Mesh corruption near water ("holes"/wrong AO, worst on beaches)** —
     `chunk_mesher` only culled faces against `is_opaque` neighbours; water is
     non-opaque, so nothing culled water-against-water internal faces. A
     submerged region could emit enough vertices to overflow raylib's
     `Mesh.indices` (`unsigned short*`, 65535 max), silently wrapping the index
     and corrupting the chunk's whole mesh — exactly the beach-adjacent
     glitches reported. Fixed: face culling (and AO sampling) now also treats
     `is_liquid` neighbours as face-blocking, so water meshes as a solid-looking
     box (proper transparency stays Phase 4.3). Added a defensive
     `kMaxMeshVertices` cap as a backstop against any future case that still
     overflows — degrades to a truncated mesh instead of corrupted geometry.
     Regression tests added (`mesher_test.cpp`): a full water chunk only meshes
     its outer shell; water-on-stone culls the shared face both ways.
     **Lesson: `raylib::Mesh` is a hard 16-bit-index format — any per-chunk
     mesh generator needs either a face-count safety margin or an explicit cap;
     there wasn't one.**

  **Follow-up (same session):** the water-culling fix didn't fully address
  "AO is wrong" — it was two separate bugs. The real AO bug: a single shared
  `kCornerUV` 4-entry `{su,sv}` sign table was reused for all 6 mesher faces,
  but each face's `kFaceCorners` winding sits differently relative to its own
  `kFaceTangents` axes; only `+X` happened to match the table by coincidence
  (`-X`/`+Z` had `u` fully inverted, `+Y` was rotated by one corner, `-Y` was
  mirrored) — AO landed on a *different* corner than the one actually
  occluded, on 5 of 6 face orientations. Fixed by computing each corner's
  `(u,v)` sign directly from its own coordinates dotted with that face's
  tangent axes (`corner_sign()`) instead of a hand-matched table — correct by
  construction, no per-face bookkeeping to get wrong. Verified the regression
  test actually catches it: reverting just the fix reproduces the bug (bright
  vertex where the occluder sits, a different vertex wrongly darkened).
  **Lesson: a shared lookup table indexed by "corner number" across 6 faces
  with independently-chosen winding + tangent-axis conventions is exactly the
  kind of thing that looks right for the first case you check (+X) and is
  wrong for the rest — derive from geometry instead of hand-deriving a table
  per case.**

  **Known, deliberately not fixed:** auto-step-up "jerks" the camera (the
  physics teleports the feet up ~1 block in a single tick — correct and
  robust, but visually abrupt). Fixing it well means smoothing the *rendered*
  eye height independently of the physics position (physics/prediction must
  stay exact; only the camera can lag), which is real work I didn't want to
  rush alongside the two correctness bugs above. Tracked in
  `REMAINING_TASKS.md` Phase 3.3 as a follow-up.

- **2026-09-11 — Phase 5.2 block breaking / placing over the network**
  (committed `e5e5640`). `C2S_BlockEdit` / `S2C_BlockEditResult` (`vb/protocol/world`) →
  **`kEngineProtocolVersion` 2 → 3**. `WorldReplicator::apply_block_edit` (reach
  ≤5.5, target validity, no-floating-placement, whole-chunk `relight_chunk`,
  builds an `S2C_ChunkDelta` with the block + diffed light bytes, fans out to
  every player whose `last_sent_` has that chunk). `ServerSession::handle_block_edit`
  routes it. `ClientSession::push_block_edit` (optimistic apply to `chunks_`,
  rollback on a `!accepted` result, drop pending on the authoritative delta) +
  `ClientChunkStore::edit_block` (bumps the chunk + bordering chunk revisions so
  the renderer re-meshes). Client `main.cpp`: Amanatides–Woo voxel raycast from
  the eye, wire-cube highlight, LMB break / RMB place stone (singleplayer path).
  Tests: `tests/unit/blockedit_test.cpp` (round-trip + 2-client fan-out + reject
  rollback).
  - **Lua veto seam:** `apply_block_edit` has a `[Phase 4.2]` comment where the
    `block_break`/`block_place` handler hooks in; region protection too.
  - **Not done:** cross-chunk light propagation on edit (relight is per-chunk),
    break progress / tool times (instant break), drops (needs items — Phase 5.1),
    `S2C_BlockEditResult` reason codes.

- **2026-09-11 — Phase 4.1 Lua VM** (committed `2e3d297`). `inc/vb/script/vm.hpp` +
  `src/script/vm.cpp`: `vb::script::Vm` — pImpl over one `sol::state`, sandboxed
  at construction (base/string/table/math/coroutine/utf8 only; os/io/load/require/
  package/collectgarbage nilled; `debug` trimmed to `traceback`), ceiling
  allocator, per-call `lua_sethook(LUA_MASKCOUNT)` instruction budget. `do_string`
  is source-only. `ScriptError` enum added to `core/error.hpp`. `tests/unit/
  script_test.cpp` (7 cases, `#if VB_WITH_LUA`). CI build workflows now pass
  `-DVB_WITH_LUA=ON` (Lua is pure C, no system deps; sol2 header-only).
  - **sol2 pin bumped v3.3.0 → v3.5.0**: 3.3.0's bundled "better optional"
    (`optional_implementation.hpp`) fails to compile under Clang ≥ 18
    ("no member named 'construct' in optional<T&>"). `docs/lua-api.md` +
    `Dependencies.cmake` note it.
  - **vm.cpp compiles in every build**: `#if !VB_WITH_LUA` gives a `kDisabled`
    stub, so `vb_core` always has the symbols and `script_test.cpp` links.
  - **Local dev:** `cmake -S . -B build-lua -DVB_WITH_LUA=ON -DVB_WITH_COMPRESSION=ON`
    — first configure re-fetches lua + sol2 (~45s). `build/` stays Lua-off.
  - **Not thread-safe:** the instruction hook assumes one `Vm` per thread (fine —
    the pack VM lives on the server tick thread).
  - **Next (4.2):** `vb.register_*` + event bus + `vb.world` API on top of `Vm`;
    (4.3) `S2C_BlockRegistry`; (4.4) asset sync + virtual FS `require`.

- **2026-09-11 — Phase 3 physics + netcode slice** (committed `37e39a3`). New
  `vb/physics/movement.{hpp,cpp}` (`step_movement` — shared server + client
  prediction; substepped swept-AABB w/ bisection snap, gravity/friction/jump/
  step-up/fly; `MoveParams`). `vb/protocol/input.{hpp,cpp}` (`InputCmd` /
  `C2SInputBatch`, lane 4). `S2C_EntitySnapshot` gained `has_local` + `local`
  record + `flags` bit0=on_ground → **`kEngineProtocolVersion` 1 → 2**.
  `ServerSession`: `handle_input_batch` (authoritative movement, dt clamp,
  seq dedup), per-`Conn` `MoveState`; snapshots now carry the recipient's own
  state. `ClientSession`: `push_input` (predict + history ring + resend),
  reconcile-on-snapshot, `remote_samples_` + `interpolated_pos`. Client
  `main.cpp` walks input-driven (collides with terrain) instead of free-fly.
  `inc/vb/ecs/components.hpp` (struct defs only). Tests: `physics_test.cpp`,
  `netcode_test.cpp`. ~99.5k assertions, green under `-Werror`.
  - **Gotcha:** step-up in a pure-voxel world needs `step_height > 1.0` to climb a
    full block (default is `1.05`); Quake-style 0.55 climbs nothing here.
  - **Gotcha:** `ServerSession` intercepts *all* post-join C2S frames now (input
    batch handled, others ignored) — it no longer forwards them to the handshake
    FSM. Fine today; revisit when C2S block-edit/chat land.
  - **Deferred:** EnTT registry + system runner (3.1), wall-clock server-time
    estimation + librg entity mapping (need real `GnsTransport`).

- **2026-09-11 — Phase 2 complete** (`ce7ee63`..HEAD). `vb/world`:
  `PalettedChunkStore`, `Chunk`, `World`, `BlockRegistry`, `LightEngine`
  (per-chunk flood fill), `chunk_codec` (RLE), `chunk_interest`,
  `ChunkLifecycleSystem`, `ClientChunkStore`, `chunk_mesher` (face-cull + AO).
  `vb/worldgen`: deterministic `vb/core/noise.hpp` (no trig, `-ffp-contract=off`
  project-wide), `WorldGenerator` (fBm heightmap), `WorldGenWorkerPool`
  (+`kSynchronous`). `vb/protocol`: `S2C_EntitySnapshot`, `S2C_Chunk*`.
  `net/world_replicator`: per-tick chunk streaming, wired into `ServerSession`
  (`set_world_replicator`) + `ClientSession`. `vb/render/chunk_renderer`: raylib
  GPU upload. `voxel_browser --singleplayer` = worldgen + streaming + meshing +
  rendering, one code path. Determinism golden `0x021BB3847413D8A5` green on
  all 3 platforms. ~99k test assertions.
- **GCC gotcha:** `uint64_t` (`unsigned long` on LP64) vs `...ULL` literals
  (`unsigned long long`) trips `-Wsign-conversion` — always name wide constants
  `constexpr std::uint64_t`.
- **Reference-lifetime gotcha:** don't store `const BlockRegistry&` — callers
  pass `BlockRegistry::base()` temporaries. `LightEngine` holds it by value.

- **2026-09-10 — Phase 1.4 replication** (uncommitted). librg spike done →
  `docs/replication.md` + `ARCHITECTURE_SPEC.md §19 Q3` (librg v7.4.0 is a
  self-contained header, zpl bundled; use it for culling + create/update/remove
  framing, our codec for payloads; not wired yet). `Dependencies.cmake` librg
  block fixed (`v7.4.0`, `vb_librg` INTERFACE target, no separate zpl).
  `inc/vb/replication/interest.hpp` (`InterestGrid` + `diff_interest`, header-
  only, linear scan). `vb/protocol/snapshot.{hpp,cpp}` (`S2CEntitySnapshot`).
  `ServerSession` broadcasts per-player snapshots each tick;
  `ClientSession::remote_entities()`; `ServerSession::set_player_state()`.
  `tests/unit/replication_test.cpp` — two-client visibility (Phase 1 exit
  criterion). 46 cases / 280 assertions.
- **CI gotcha (found on first push):** `doctest.h` trips MSVC `/W4 /WX`
  (`C2220` at doctest.h:539). Fix: `tests/CMakeLists.txt` re-adds
  `doctest::doctest`'s include dir as `SYSTEM PRIVATE`. clang/gcc never hit it,
  so it must be verified in CI. Same pattern already used for raygui/toml++.

- **2026-09-10 — Phase 1.1 TOML config** (uncommitted). `tomlplusplus` v3.4.0
  added to `Dependencies.cmake` (always-on, header-only, linked PRIVATE into
  `vb_core`). `vb/core/config.{hpp,cpp}`: `ServerConfig`/`ClientConfig` (spec
  §15 defaults), `parse_*`/`load_*` (missing file → defaults, malformed →
  `kParseError`), `apply_cli_overrides`. Both binaries load `--config` +
  overrides. `server.toml.example` / `client.toml.example` in repo root;
  `/server.toml` `/client.toml` gitignored. `tests/unit/config_test.cpp`.
  41 test cases / 242 assertions green.

- **2026-09-10 — Phase 1.5 client shell** (uncommitted). `vb/render/camera.hpp`
  — header-only `FirstPersonController` (double precision throughout; the strict
  `-Wdouble-promotion` warning makes mixed float/double painful, so everything
  angle/position is `double` and `LookMoveInput` holds `Vec2d`/`Vec3d`). Client
  `main.cpp` gained mouse-look + WASD + a debug overlay + spawn-from-JoinAccept.
  `tests/unit/render_test.cpp` (5 cases). Rule: no `1.0f` literals anywhere the
  value flows into a `double` — CI's `-Wdouble-promotion` is fatal.

### Gotchas learned this pass
- Heavy deps (GNS, librg, Lua/sol2, FastNoise2, LZ4/xxHash, Cellulose) are
  declared in `Dependencies.cmake` but **gated behind `VB_WITH_*` (default OFF)**.
  Phase 1 flips `VB_WITH_NET` / `VB_WITH_REPLICATION` on — expect the GNS build
  (protobuf + OpenSSL) to be the real work there, and the CI `apt`/`brew`/`choco`
  steps to need new packages then.
- `raylib` + `raygui` are only fetched when `VB_BUILD_CLIENT=ON`.
- raygui's implementation must be its own target (`vb_raygui_impl`) so project
  `-Werror` never touches it.
- `src/**` in the old lint job didn't recurse; lint now uses `find`. Every file
  under `src/` (incl. `.c`) must be clang-format-clean or CI fails.
- doctest + MSVC STL: comparing/streaming a `std::string_view` in a `CHECK`
  instantiates `toString<string_view>` which needs `<ostream>` complete —
  add `#include <ostream>` to any test TU that does this (see `core_test.cpp`).
- `Result<T,E>`: never call `.error()` when it holds a value (union UB). In
  tests use `REQUIRE(result)` then deref, not `REQUIRE_MESSAGE(result, msg(err))`.
- Sub-modules attach sources to `vb_core` via `target_sources()` from their own
  `src/<mod>/CMakeLists.txt`, added after `core` in `src/CMakeLists.txt`.
- Local dev on this Windows box: `clang`/`clang++` (LLVM 21) work; no `gcc`/`cl`
  on PATH in git-bash. `CC=clang CXX=clang++ cmake -G Ninja` configures fine.
  First configure is slow (~130s) — raylib + deep git clones.
