# STATE — Working Notes for Future Agents

> Living scratchpad of gotchas, landmines, open decisions, and small TODOs
> discovered while working in this repo. **Update this file** when you learn
> something non-obvious or fix something listed here.
>
> Companion docs: `ARCHITECTURE_SPEC.md` (target design) · `REMAINING_TASKS.md`
> (implementation backlog). This file is for *traps and context*, not the plan.

Last updated: 2026-09-11 (smoke-test bugfixes)

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
- `build_linux.yml` installs X11/GL dev packages for raylib. When GNS / other
  deps land, each build workflow's apt/brew/choco step needs the new system
  deps (OpenSSL, protobuf for GameNetworkingSockets especially).
- `runner.yml` fires on push to `main` and `*-workflow` branches, plus PRs.

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
- **GameNetworkingSockets** is the heavy dependency: needs protobuf + a crypto
  backend (OpenSSL or libsodium/bcrypt). This dominates build setup work in
  Phase 0/1. Consider whether to vendor via `FetchContent` (slow, pulls
  protobuf) or require a system install.
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

- **2026-09-11 — Bugfixes from the first manual smoke test** (uncommitted).
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
