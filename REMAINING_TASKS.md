# Voxel Browser — Remaining Tasks

> Companion to `ARCHITECTURE_SPEC.md`. This is the implementation backlog to get
> from the current state to the "Minimum Playable Base" described in `README.md`.

## Current State (baseline)

- [x] Modular CMake: `vb_core` / `vb_render` / `voxel_browser` /
      `voxel_browser_server` / `vb_tests`, git-tag versioning, `PROJECT_NAME`
- [x] `cmake/{Dependencies,Warnings,Sanitizers}.cmake`; deps via pinned `FetchContent`
- [x] Runnable client + server skeletons (arg parsing, banners, loops, `--headless`)
- [x] CI: lint + Linux/macOS/Windows build matrix **+ ctest**, bundle, publish
- [x] `.clang-format` (tabs, `ColumnLimit 0`, `Standard: c++20`)
- [ ] Networking / world / ECS / scripting — Phase 1 onward

Legend: `[ ]` todo · `[~]` in progress · `[x]` done · **(spike)** = timeboxed
investigation, may change the plan.

---

## Phase 0 — Project Restructure & Build System  ✅ done (2026-09-10)

Prerequisite for everything else. Not in the README's phase list but required.

- [x] Split the monolith target into `vb_core` (static lib), `vb_render` (static
      lib), `voxel_browser` (client exe), `voxel_browser_server` (server exe),
      `vb_tests` (test exe). `add_subdirectory` per module under `src/`.
- [x] Create the `inc/vb/...` + `src/...` directory tree from spec §4
      (module dirs stubbed with `.gitkeep` until their phase).
- [x] `cmake/Dependencies.cmake`: `FetchContent` (pinned tags) + `find_package`
      fallback for each dependency, mirroring the raylib block. Heavy / spike-
      blocked deps are declared but gated behind `VB_WITH_*` (default OFF) and
      pulled in by the phase that owns them:
    - [x] EnTT (`v3.13.2`, linked now)
    - [x] raygui (`4.0`) + isolated implementation TU (`src/render/raygui_impl.c`)
    - [x] doctest (`v2.4.11`, linked now)
    - [x] FastNoise2 (`v0.10.0`) — `VB_WITH_WORLDGEN`
    - [x] GameNetworkingSockets (`v1.4.1`) — `VB_WITH_NET`; transitive protobuf +
          OpenSSL documented in the Linux workflow + `docs/protocol.md`
    - [x] librg + zpl (`v7.2.2` / `v18.1.4`) — `VB_WITH_REPLICATION` (confirm API in the spike)
    - [x] Lua 5.4 (`v5.4.6`) — `cmake/lua/CMakeLists.txt` wrapper — `VB_WITH_LUA`
    - [x] sol2 (`v3.3.0`) — `VB_WITH_LUA` (open question #1 resolved: sol2)
    - [x] xxHash (`v0.8.2`), LZ4 (`v1.9.4`) — `VB_WITH_COMPRESSION`
- [x] `cmake/Warnings.cmake`: `-Wall -Wextra -Wpedantic …` / `/W4`,
      `-Werror` gated behind `VB_WARNINGS_AS_ERRORS` (CI sets it), `vb_warnings`
      INTERFACE target linked PRIVATE by first-party targets only.
- [x] Sanitizer options: `VB_ENABLE_ASAN/_UBSAN/_TSAN` (`cmake/Sanitizers.cmake`).
- [x] `VB_HEADLESS` option — `window.cpp` no-ops every GL call; same client code
      path runs without a GPU.
- [x] Update the 3 build workflows for the new targets (stage both binaries) +
      note new system deps; `upload-artifact@v7` typo → `v4`.
- [x] Test coverage on all platforms: each build workflow now runs `ctest`
      (`vb_tests` + `server_smoke` + `client_smoke`).
- [x] Replace `src/main.cpp` demo with `src/client/main.cpp` +
      `src/server/main.cpp` (arg parsing, build banner, render-loop / tick-loop
      skeletons, headless smoke path).
- [x] `docs/` folder; `ARCHITECTURE_SPEC.md` kept at root, stub `docs/protocol.md`
      + `docs/lua-api.md` added.
- [x] Project name decided: **`voxel_browser`** (`PROJECT_NAME` updated).
- [x] Build-blocking bugs from `STATE.md` §1 fixed: generated `vb/core/version.hpp`
      (no more bare `PROJECT_VERSION`); raylib pinned `GIT_TAG 5.5` + `GIT_SHALLOW`;
      `cmake_minimum_required` → 3.25.

Follow-ups deferred out of Phase 0:
- [ ] First `git tag v0.0.1` so `git describe` yields a real version and
      `bundle`/`publish` are exercised.
- [ ] `CMAKE_POLICY_VERSION_MINIMUM=3.5` shim is set for CMake ≥ 4 (doctest
      2.4.11 declares `cmake_minimum_required(3.0)`); drop it if doctest is bumped.
- [ ] Explicit source lists instead of relying on re-running CMake (targets use
      explicit lists already; keep it that way as modules grow).

---

## Phase 1 — Core Foundation & Networking

Goal: reliable client↔server handshake; two clients can "see" each other in
network space; client window + render loop alive.

### 1.1 Core primitives (`vb_core/core`)

- [x] Math types (`Vec2/3<T>`, `Vec3d`, `IVec3`, `AABB`, Euclidean chunk coord
      math) — `inc/vb/core/math.hpp`. Standalone (no raylib dep).
- [x] `Result<T,E>` / `Status<E>` + `CoreError` / `ProtocolError` enums, no
      exceptions — `inc/vb/core/{result,error}.hpp`.
- [x] Logging (levelled, thread-safe, sink-based) — `inc/vb/core/log.hpp` +
      `src/core/log.cpp`, `VB_INFO`/`VB_WARN`/… macros.
- [x] Config loader (TOML) for `server.toml` / `client.toml` + CLI overrides —
      `inc/vb/core/config.hpp` + `src/core/config.cpp` (tomlplusplus v3.4.0).
      Defaults ← file ← CLI; missing file = defaults, malformed = `kParseError`.
      `*.toml.example` templates in the repo root; both binaries load and apply.
- [x] ID types: `BlockId`, `NetId`, `EntityKindId`, `ChunkCoord`, `AssetHash`
      — `inc/vb/core/ids.hpp` (+ `std::hash` specializations).

### 1.2 Transport (`vb_core/net`)

- [x] `Transport` interface: `listen` / `connect` / `send` / `close` / `poll`,
      whole-message delivery per lane, `TransportEvent` stream —
      `inc/vb/net/transport.hpp`.
- [x] `LoopbackTransport` (`inc/vb/net/loopback.hpp` + `.cpp`): in-process,
      deterministic, dependency-free backend for tests + integrated singleplayer.
- [x] `GnsTransport` over `ISteamNetworkingSockets` — `vb/net/gns_transport.{hpp,cpp}`,
      real UDP, built under `VB_WITH_NET`; a `kBackendUnavailable` stub otherwise
      (same pattern as `vb::script::Vm`). One process-wide `GnsRuntime` (refcounted
      init/shutdown, the single global connection-status callback GNS exposes)
      routes events to the owning `GnsTransport` via listen-socket/connection
      handle registries — matters for tests running a server + several clients
      in one process; application code just uses `Transport` normally. ICE/WebRTC
      off (direct dedicated-server connections only, not P2P). `bound_port()`
      (GNS-specific, not on the `Transport` interface) exposes the real port
      after `listen(0)`. Unit test: real UDP connect/send/disconnect over
      127.0.0.1 (`tests/unit/gns_transport_test.cpp`).
      **Dependency note:** GNS needs protobuf as a real *installed* package
      (its CMake config, not just a source checkout — FetchContent-ing
      protobuf's source doesn't work, see the long comment in
      `cmake/Dependencies.cmake`). Windows: vcpkg. Linux: apt
      (`libprotobuf-dev protobuf-compiler libssl-dev`). macOS: brew
      (`protobuf openssl`) — **not yet wired into CI**, see below.
      Pin bumped v1.4.1 → v1.6.0 (v1.4.1 doesn't compile under MSVC:
      `std::string_view::c_str()` doesn't exist, fixed upstream by v1.6.0).
- [x] Lane/channel configuration (§8.2): `lane_for(MessageType)` +
      `send_mode_for_lane(Lane)` (reliable vs. unreliable).
- [x] Message envelope `{type, flags, payload_len}` + varint helpers —
      `inc/vb/protocol/{byte_buffer,message}.hpp`, `read_frame`/`write_frame`.
- [x] Codec framework in `vb/protocol/`: bounds-checked `ByteReader`/`ByteWriter`,
      error-accumulating reads, round-trip + truncation + overlong-varint +
      bad-enum + trailing-byte unit tests (`tests/unit/protocol_test.cpp`).
- [x] `kEngineProtocolVersion` constant (`version.hpp`); `ProtocolError::kVersionMismatch`
      reserved. Mismatch *handling* in the handshake FSM is 1.3.

### 1.3 Handshake (§8.3)

- [x] Message structs + codecs: `C2S_Hello`, `S2C_ServerInfo`, `C2S_Auth`,
      `S2C_AuthResult`, `C2S_Ready`, `S2C_JoinAccept`, `S2C_Disconnect{reason}`
      — `inc/vb/protocol/handshake.hpp` + `src/protocol/handshake.cpp`, all
      round-trip tested. `docs/protocol.md` documents every field.
- [x] Server: `ServerHandshake` FSM (`AwaitingHello → AwaitingAuth →
      AwaitingReady → Playing`/`Closed`), `on_timeout()`, `max_players` gate,
      host hooks (`current_player_count` / `authenticate` / `on_ready`).
- [x] Client: `ClientHandshake` FSM + `ClientHandshakeStatus` enum for the UI
      (`Connecting / Authenticating / Syncing / Joined / Failed`).
- [x] `auth_mode = none` path fully working (default `authenticate` accepts any
      1–32 char name); `token` path carried through, verification stubbed.
- [x] Integration test: full handshake + auth reject + server full + out-of-order
      + version mismatch + timeout, driven over `LoopbackNetwork`
      (`tests/unit/net_test.cpp`).
- [x] Session layer: `ServerSession` (drives per-conn handshakes, timeouts,
      net-id allocation, `take_joins`/`take_leaves`) + `ClientSession` +
      `IntegratedGame` (loopback server+client in one object) —
      `inc/vb/net/{session,integrated}.hpp`.
- [x] Integrated singleplayer wired into `voxel_browser --singleplayer`
      (works headless; `singleplayer_smoke` CTest asserts the join line).
- [x] `voxel_browser_server` actually runs a game now (it only sleep-looped
      before): owns a `World` + `WorldGenWorkerPool`, listens via `GnsTransport`,
      drives one `ServerSession` + `WorldReplicator`, logs joins/leaves. Random
      seed when `world_seed = 0`. Bind address is always "any interface" for now
      (`config.bind_address` isn't wired into the actual bind yet). Built
      without `VB_WITH_NET`, `listen()` returns `kBackendUnavailable`, which is
      treated as non-fatal (warns, keeps ticking with no one able to connect) —
      every *other* `listen()` failure (port in use, bind denied) is still
      fatal. Keeps the binary testable/CI-runnable without the heavy dep, same
      spirit as `vb::script::Vm` degrading without `VB_WITH_LUA`.
- [x] `voxel_browser` connects for real: `--server`/`--port` build a
      `GnsTransport` + `ClientSession` (`RemoteConnection` in `main.cpp`), same
      render/prediction/block-edit loop `--singleplayer` already used, unified
      behind one `ClientSession*` regardless of which path is active.
- [ ] Per-IP connection cap belongs to the server loop.
- [ ] `ENGINE_PROTOCOL_VERSION` mismatch → both FSMs already reject; surface it
      in the client connect UI (Phase 5.3 main menu).
- [ ] Hostname resolution: `connect()` only accepts numeric IP literals today
      (`SteamNetworkingIPAddr::ParseString` doesn't resolve DNS). "localhost" /
      real hostnames need `getaddrinfo` in `GnsTransport::connect`.
- [ ] macOS CI doesn't build `VB_WITH_NET` yet: it's a universal (arm64+x86_64)
      build, but a brew-installed protobuf is single-arch, which breaks linking
      the other slice. Needs a universal protobuf (vcpkg triplet, or building
      protobuf from source for both arches) — see `build_macos.yml`.
- [ ] Two live `voxel_browser` + `voxel_browser_server` processes have not
      been run against each other manually yet — validated so far by
      `gns_transport_test.cpp` (raw transport, one process) and the existing
      Loopback-based session/handshake/replication tests (application logic,
      generically transport-agnostic). Worth an actual two-terminal smoke test.

### 1.4 Replication bootstrap (`vb_core/replication`)

- [x] **(spike)** librg v7.4.0 API investigated — findings + the §19 Q3 decision
      in `docs/replication.md` (use librg for culling + create/update/remove
      framing; keep our own payload codec). Pin fixed in `Dependencies.cmake`
      (self-contained header, no separate zpl).
- [x] Hand-rolled interest first: `InterestGrid` + `diff_interest`
      (`inc/vb/replication/interest.hpp`) with the same diff semantics behind a
      narrow interface, so librg drops in underneath later.
- [x] `S2C_EntitySnapshot` (`inc/vb/protocol/snapshot.hpp`): `server_tick`,
      `last_acked_input_seq`, `entered[]` / `updated[]` / `removed[]` records.
      Round-trip tested.
- [x] `ServerSession` builds per-player snapshots each tick from the interest
      grid (diff vs. last-visible); `ClientSession` applies them into
      `remote_entities()`. `set_player_state()` feeds authoritative positions.
- [x] Integration test (`tests/unit/replication_test.cpp`): two headless clients
      join one server; each sees the other only within interest range, gets a
      `removed` when the other moves out or disconnects. **(README Phase 1
      acceptance — met over the loopback transport.)**
- [ ] Wire real librg in (`VB_WITH_REPLICATION`) as the interest backend — after
      players actually move (Phase 3) so scale testing is meaningful.
- [ ] The two-client test currently runs over `LoopbackTransport`; re-run it over
      `GnsTransport` once that lands.

### 1.5 Client shell (`vb_render`)

- [x] Window/context via raylib, main loop scaffold, `--headless` no-op renderer
      (`vb/render/window.hpp`).
- [x] `FirstPersonController` (`vb/render/camera.hpp`, header-only, unit-tested):
      mouse-look with pitch clamp, WASD + vertical + sprint, normalized diagonals,
      double precision. Client fills `LookMoveInput` from raylib; mouse-capture
      toggle on click / Tab / Esc.
- [x] Debug overlay: position, yaw/pitch, FPS, connection status line.
- [x] Placeholder grid/cube render until Phase 2 meshing.
- [x] Spawn/orient from `S2C_JoinAccept` in the *multiplayer* path too — the
      Phase 1.2 `main.cpp` refactor unified singleplayer and remote play
      behind one `ClientSession*`, so both read `spawn_pos` the same way now.

**Phase 1 exit:** `voxel_browser_server` accepts connections; `voxel_browser`
connects, completes handshake, opens a window; integration test for mutual
visibility of two clients is green in CI.

**Phase 1 status (2026-09-11): met.** `GnsTransport` (real UDP over
GameNetworkingSockets) landed alongside everything from 2026-09-10 (core
primitives, TOML config, wire codec, handshake FSMs, session layer, client
shell, interest management + entity snapshots). Both binaries are wired to it:
the dedicated server actually runs a game now instead of an empty tick loop,
and the client's non-singleplayer path connects for real. The librg
integration is still deferred to Phase 3 (needs moving players to be
meaningful) — the `Transport` seam it needs is unaffected either way. Not yet
done: re-running the multi-client Loopback tests over `GnsTransport` (the raw
transport has its own real-UDP test instead), hostname resolution, and macOS
CI (see 1.2's notes).

---

## Phase 2 — World State & Terrain Generation

Goal: server generates terrain, streams chunks, client meshes and renders them.

### 2.1 Voxel data model (`vb_core/world`)  ✅

- [x] `PalettedChunkStore` — 0/1/2/4/8/16 bpv, auto width growth, homogeneous
      collapse, `compact()`; randomized round-trip test.
- [x] `Chunk` — block store + light volume (sky/block nibbles) + `DirtyFlags` +
      `GenState` + monotonic `revision`.
- [x] `World` — `ChunkCoord → Chunk` map, world-voxel get/set across boundaries,
      load/unload; implements `BlockSolidQuery`.
- [x] `BlockSolidQuery` interface. `BlockRegistry` (hardcoded base set; Phase 4
      → Lua). `kChunkDim = 32` + `index_of` / `address_of` coord math.

### 2.2 World generation (`vb_core/worldgen`)  ✅ (base pipeline)

- [x] Deterministic hand-rolled coherent noise (`vb/core/noise.hpp`) — integer
      hash + polynomial interpolation, no trig; `-ffp-contract=off` project-wide
      for cross-compiler reproducibility. (FastNoise2 remains the Phase 4
      Lua-pipeline backend behind `VB_WITH_WORLDGEN`.)
- [x] Fixed base pipeline (`WorldGenerator`): fBm heightmap → stone / dirt /
      grass, sand + water near sea level. Deterministic from (seed, coord).
- [x] `WorldGenWorkerPool` — N threads, dedup, `poll_completed()` handoff to the
      tick thread (mutex+CV queue; "lock-free" is aspirational, correctness first).
- [x] Determinism gate: `tests/unit/worldgen_test.cpp` hashes a fixed 4-chunk
      region (FNV-1a) against a committed golden — CI runs it on all 3 platforms.
- [ ] Biome selection (2.2 step 2) + carvers + decoration pass — deferred to the
      Lua pipeline (Phase 4); base pipeline is heightmap-only for now.

### 2.3 Lighting  ✅ (per-chunk)

- [x] `LightEngine::relight_chunk` (`vb/world/lighting.{hpp,cpp}`): BFS sky-light
      flood (full-strength straight down, −1/step sideways) + block-light flood
      from emitters; `transmittance()` from the registry (opaque = 0, water dims
      by 2). Clears the light dirty flag, marks mesh dirty.
- [x] Cross-chunk **vertical** sky occlusion + relight-on-edit cascade
      (`relight_column` in `lighting.hpp`, 2026-09-15): a chunk's relight now
      uses its real neighbour above (or cascades down through a whole loaded
      column) instead of always assuming open sky. Fixed the false-bright
      band at chunk boundaries reported while mining. See `STATE.md` §8.
- [ ] Horizontal cross-chunk light propagation (sideways-only spill, e.g.
      under a horizontal overhang spanning a chunk border) is still
      per-chunk-only — smaller-magnitude follow-up, not attempted.

### 2.4 World replication (§8.5)  ✅

- [x] Messages: `S2C_ChunkAdd` (coord + revision + opaque payload),
      `S2C_ChunkDelta` (block + light change lists), `S2C_ChunkRemove` —
      `vb/protocol/world.{hpp,cpp}`, round-trip tested.
- [x] Palette container serialization (`vb/world/chunk_codec`): palette + RLE of
      (run, palette-index) + RLE'd light. LZ4 is the `MessageFlag::kCompressed`
      framing layer, wired when `VB_WITH_COMPRESSION` lands (not required for
      correctness; RLE already shrinks it well).
- [x] Per-player visible chunk set (`vb/world/chunk_interest.hpp`:
      `chunks_in_view` + `diff_chunk_sets`) → add/remove diff in
      `net/world_replicator.{hpp,cpp}`.
- [x] `ChunkLifecycleSystem` (`vb/world/chunk_lifecycle.{hpp,cpp}`):
      generate (worldgen pool) / light / load around players, unload when no
      player wants a chunk. Worker pool gained a `kSynchronous` mode for
      deterministic tests + the integrated server.
- [x] `ClientChunkStore` (`vb/world/client_chunk_store.{hpp,cpp}`): applies
      add/delta/remove, implements `BlockSolidQuery` for meshing + prediction.
- [x] Wired into `ServerSession` (`set_world_replicator`) + `ClientSession`
      (auto-mirrors chunk messages post-join). Integration test: a joined client
      mirrors the 27-chunk box around its spawn and reclaims it on move.

### 2.5 Client meshing  ✅ (hand-rolled; Cellulose swap-in pending)

- [x] **(spike)** Cellulose API — resolved in `ARCHITECTURE_SPEC.md §19 Q2`.
      Emits vertex data (`ChunkMesh`); reusable seam is `greedy_mesh(vector<
      MeshSample>, …)`. Blocked on `VB_WITH_MESHING` (submodule + demo-raylib
      fetch); `Dependencies.cmake` prepared.
- [x] `vb/world/chunk_mesher.{hpp,cpp}`: face-culled cube mesh from a
      `ClientChunkStore` (reads neighbours across chunk borders), per-vertex
      light + ambient occlusion. `MeshData { MeshVertex[], u32 indices[] }` —
      renderer-neutral. Same I/O as `greedy_mesh` so the swap is local.
- [x] `vb/render/chunk_renderer.{hpp,cpp}`: main-thread meshing with a per-frame
      budget, raylib GPU upload (vertex colours from light × per-block tint),
      drop-on-unload, re-mesh on `revision` change.
- [x] Wired into the client: `--singleplayer` keeps an in-process
      `IntegratedGame` + `World` + worldgen pool + `WorldReplicator` alive; the
      render loop feeds player position back and draws the streamed, meshed
      terrain.
- [x] Mesh worker pool (`vb/world/chunk_mesh_worker_pool.{hpp,cpp}` +
      `chunk_mesh_snapshot.{hpp,cpp}`, 2026-09-15): CPU face-culling/AO now
      runs on background threads from a per-chunk+1-voxel-border snapshot;
      `ChunkRenderer::sync()` only does the GPU upload on the main thread.
      Fixes framerate drops while chunks stream in. See `STATE.md` §8.
- [ ] Frustum culling, transparent second pass, texture atlas (Phase 4) —
      follow-ups.

**Phase 2 exit:** connect to a server and fly around streamed, meshed terrain
(dirt/stone/grass/air) with correct chunk load/unload; determinism CI gate green.

**Phase 2 status (2026-09-11): met.** `voxel_browser --singleplayer` generates
terrain, streams it as chunks, meshes and renders it, and loads/unloads chunks
as the player moves. Determinism gate is green on all 3 platforms. Deferred to
later phases: biomes/carvers/decoration (Lua pipeline, Phase 4), cross-chunk sky
occlusion + relight-on-edit (Phase 3/5), mesh worker pool + greedy merge
(Cellulose, `VB_WITH_MESHING`), texture atlas (Phase 4), LZ4 chunk compression
(`VB_WITH_COMPRESSION`).

---

## Phase 3 — Entity Component System & Physics

Goal: server-simulated players with voxel collision, movement synced to clients
with prediction/interpolation.

### 3.1 ECS (`vb_core/ecs`)

- [x] Base components (§7.1) defined — `inc/vb/ecs/components.hpp` (`Position`,
      `Velocity`, `Rotation`, `Collider`, `PlayerInput`, `PlayerTag`,
      `NetReplicated`, `EntityKind`, `Health`, `Inventory`, `ItemStack`,
      `InterpBuffer`). `ScriptState` waits for Phase 4.
- [ ] EnTT registry wiring on the server; fixed 20 Hz tick loop with accumulator.
      **(deferred — the session drives per-player movement directly for now;
      the registry + `SystemRunner` is a refactor once Lua entity kinds (Phase 4)
      need to iterate arbitrary entities.)**
- [ ] System runner with explicit ordering (§7.2).
- [ ] Client-side lightweight registry — currently `ClientSession` holds the
      predicted local state + a `remote_samples_` interp buffer inline.

### 3.2 Input pipeline

- [x] `InputCmd { seq, dt, move, yaw, pitch, buttons }` + `C2S_InputBatch`
      (lane 4) — `inc/vb/protocol/input.hpp`, round-trip tested. Client keeps an
      unacked history ring (`ClientSession::push_input`) and resends it each frame.
- [x] Input ingest: `ServerSession::handle_input_batch` — skips already-simulated
      `seq`, clamps `dt` to [0, 0.1], caps the batch at 64 cmds on decode.
- [ ] Per-player rate limit / flood guard belongs with `GnsTransport`.

### 3.3 Physics (`vb_core/physics`)  ✅

- [x] `vb/physics/movement.{hpp,cpp}`: `step_movement()` — substepped per-axis
      swept-AABB voxel collision (bisection snap-to-contact), ground friction +
      wish-velocity accel, gravity, jump, `on_ground` probe, full-voxel step-up,
      fly mode. Parameterized by `BlockSolidQuery`; no raylib/ECS.
- [x] One implementation consumed by the server (`handle_input_batch`) and client
      prediction (`ClientSession`).
- [x] `MoveParams` tunables (speeds, gravity, jump, step height); `ServerSession`
      / `ClientSession` `set_move_params`. Config wiring (`server.toml`) → Phase 5.
- [x] Unit tests (`tests/unit/physics_test.cpp`): floor rest, no tunnelling at
      terminal velocity, wall stop + slide, jump arc, step-up, yaw basis,
      sustained speed reaches walk/sprint, friction stops on release.
- [ ] **Follow-up (smoke test):** step-up teleports the feet up to a full block
      in one physics tick — correct, but visually abrupt ("jerk"). Physics
      must stay exact for prediction/reconciliation; the fix is a
      render-only eye-height smoothing layer in the client (lerp the rendered
      camera Y toward the true feet+eye position, capped so it doesn't lag
      behind normal fall/jump motion) — not attempted yet.
- [x] Join-time fall-through-world / embedding fix: `physics::
      ground_area_loaded()` freezes `step_movement` (both server and client)
      until the spawn column's chunk + 2 below are loaded, so a player can't
      free-fall through not-yet-generated terrain and end up stuck inside it
      once the chunk arrives. Unit-tested (5 cases); **not** covered by an
      end-to-end integration test — the race needs the real async
      `WorldGenWorkerPool`, which existing integration tests avoid via
      `kSynchronous` (generates instantly, structurally can't reproduce the
      gap). See `STATE.md` for the full reasoning if this needs revisiting.
- [x] Fixed spawn *position* itself (a separate bug from the one above):
      `JoinGrant::spawn_pos` defaulted to a fixed `{0, 64, 0}` regardless of
      seed — 63% of 30 probed seeds had a real surface height `>= 64` at
      that column, embedding the player in solid terrain outright, no
      falling needed. Singleplayer's hardcoded seed 7 masked this for all of
      Phase 3-5 (its surface there happens to be 56). Fixed via
      `worldgen::default_spawn_position()` wired into a
      `HandshakeServerHost::on_ready` in both `--singleplayer` and the
      dedicated server. Tests in `tests/unit/worldgen_test.cpp`.

- [x] `S2C_EntitySnapshot` carries `last_acked_input_seq` + the recipient's own
      authoritative `local` record (interest culling excludes self). `flags` bit 0
      = `on_ground`.
- [x] Local-player prediction + reconciliation — `ClientSession`: predict on every
      `push_input`, on each snapshot snap to `local` and replay the unacked history
      through the shared `step_movement`.
- [x] Remote entity interpolation — `remote_samples_` keeps two snapshots per id;
      `interpolated_pos()` lerps ~1 tick behind. (Tick-indexed, not wall-clock.)
- [x] Integration test (`tests/unit/netcode_test.cpp`): input batch round-trip;
      predicted feet converge to server authority within epsilon; a second client
      sees the first move.
- [ ] Wall-clock `server_time_est` + smoothing on the client (needs `GnsTransport`
      RTT; the loopback path has no latency to estimate).
- [ ] Map players ↔ librg network entities — with the rest of `VB_WITH_REPLICATION`.

### 3.5 Entity visual presentation — billboard sprites (§11.3)  ✅ (placeholder art)

**Gap closed:** nothing used to draw a remote player or entity at all —
`remote_entities()`/`interpolated_pos()` (3.4) gave correct positions, but the
client only rendered terrain. Design: `ARCHITECTURE_SPEC.md` §11.3 (Don't
Starve-style Y-axis-billboarded, directionally-animated sprites, decided
2026-09-11 — see §19 Q7).

- [x] `vb/render/entity_renderer.{hpp,cpp}` (sibling to `chunk_renderer`):
      per-`NetId` render state (current clip, elapsed time, direction bucket +
      hysteresis); draws each tracked remote entity as one `DrawBillboardPro`
      call per frame (`up = {0,1,0}` for the Y-axis lock — raylib's `right`
      there comes from the view matrix and is always horizontal regardless of
      pitch, confirmed directly in `rmodels.c`, no custom quad math needed).
      Only constructed when the window isn't headless (same pattern as
      `ChunkRenderer`).
- [x] Direction-bucket selection: `vb/render/entity_visual.hpp` —
      `bearing_degrees` / `direction_bucket` / `select_pose` /
      `DirectionBucketTracker` (header-only, no raylib, unit-tested like
      `camera.hpp`). Bearing from entity to camera minus the entity's own yaw,
      bucketed into `facings` sectors (default 8, active today even against
      the flat placeholder), hysteresis so standing near a sector boundary
      doesn't flicker. Mirroring via negative `size.x` in `DrawBillboardPro`.
- [x] Client animation state machine — `resolve_anim_clip()`: priority `dead >
      hurt_pulse > acting > jump/fall > run > walk > idle` from
      `EntityRecord.vel` (speed thresholds) + `flags` bits. **Still open:** the
      server only ever sends `flags` bit 0 (`on_ground`); `dead`/`hurt_pulse`/
      `acting` are defined (`EntityAnimFlag`) but nothing sets them yet, so
      only jump/fall/run/walk/idle are reachable in practice today. Wiring the
      other bits bumps `kEngineProtocolVersion` + `docs/protocol.md`.
- [ ] `vb/ecs/components.hpp` gains a `SpriteVisual` component (atlas handle,
      `facings`, per-clip frame lists/fps/loop) — the kind's static visual def.
      Deferred: nothing produces one until 4.2 exists; `EntityRenderer` today
      hardcodes `facings = 8` and a single flat frame instead of reading a
      per-kind def.
- [x] **Hardcoded fallback, no Lua/pack dependency**: a 1×1 white texture
      tinted per-`NetId` (deterministic hash → hue, so distinct entities are
      distinguishable) stands in for real art — mirrors how Phase 2 shipped a
      hand-rolled mesher ahead of Cellulose.
- [ ] Real content (atlas art, per-clip frame data) is pack-defined —
      `vb.register_entity{ visual = {...} }` (4.2) + the atlas travels over Asset
      Sync (4.4) like any texture; base-pack sprites are 5.1.
- [x] Local player: **not** billboarded in first-person (no viewmodel in scope);
      third-person / spectator views are future work.
- [x] Unit tests (`tests/unit/entity_visual_test.cpp`, 14 cases): anim-priority
      resolution, bearing convention, direction-bucket math (front/back/yaw
      invariance), pose mirroring for `facings` 8/4/1, tracker hysteresis
      (ignores a flicker, switches once stable), `EntityPresentationState`
      clip-time reset/accumulation.
- [ ] Non-goals for v1 (explicitly deferred, not forgotten): skeletal/vertex
      animation, per-limb equipment layering, blob shadows, dynamic per-entity
      lighting from the block they stand in (chunks already compute this — cheap
      follow-up, not core).

**3.5 status (2026-09-11): the machinery is real and tested; the art is not.**
`--singleplayer` will draw a flat colored quad, correctly billboarded and
direction/animation-tracked, for every remote entity — there just aren't any to
see without a second connected player yet (needs `GnsTransport`, or a future
in-process 2-client harness). Verified via the automated netcode/replication
tests exercising `remote_entities()`, and via `entity_visual_test.cpp` for the
presentation logic itself; not yet eyeballed with two live windows.

**Phase 3 exit:** two players walk around shared terrain, colliding with voxels,
**visibly** smooth on each other's screens (3.5), local motion is responsive
(predicted).

**Phase 3 status (2026-09-11): substantially met over the loopback transport,
including visual presentation.** Shared voxel physics, the input pipeline,
authoritative server movement, client prediction/reconciliation, remote
interpolation, and now billboard rendering of remote entities (3.5) are done
and tested; the client (`--singleplayer`) walks input-driven with terrain
collision instead of the free-fly cam. Deferred: a formal EnTT registry + system
runner (Phase 3.1 — not blocking; revisit when Lua entities need it), wall-clock
server-time estimation and the librg entity mapping (both need the real
`GnsTransport`), and 3.5's real art (waits on 4.2/4.4/5.1) — the placeholder
billboards are drawn and tested but haven't been eyeballed with two live
players in one session yet.

---

## Phase 4 — The "Browser" Engine (Scripting & Assets)

Goal: server logic + content defined in Lua; client auto-downloads pack assets;
Lua-defined UI.

### 4.1 Lua runtime (`vb_core/script`)  🚧

- [x] Embed Lua 5.4 (`cmake/lua` wrapper) + sol2 **v3.5.0** (bumped from 3.3.0 —
      Clang ≥ 18 incompat). `VB_WITH_LUA` on in all three CI build workflows.
- [x] `vb::script::Vm` — pImpl over `sol::state`; `do_string` (source only,
      traceback capture), `ScriptResult` / `core::ScriptError`. Stub build when
      `VB_WITH_LUA` is off.
- [x] Sandbox env (§10.2): curated libs, `os`/`io`/`load`/`require`/`package`/
      `collectgarbage` nilled, `debug` → `traceback` only, instruction-count
      hook, ceiling allocator.
- [x] Sandbox-escape tests (`tests/unit/script_test.cpp`): os/io/load absent,
      runaway loop → `kBudgetExceeded`, memory bomb → `kOutOfMemory` (recoverable).
- [ ] Custom `require` over the virtual pack FS + per-callback wall-clock budget
      — deferred to 4.4 (needs the synced asset FS).
- [ ] Decision #1 (sol2) — recorded; **done**.

### 4.2 Server Lua API (§10.3)  🚧

- [x] `vb::script::PackRuntime` (`inc/vb/script/pack_runtime.hpp` +
      `src/script/pack_runtime.cpp`) — pImpl'd like `Vm`, stub build when
      `VB_WITH_LUA` is off. `Vm` gained `native_impl()` + a new internal
      `vm_internal.hpp` (shared `Vm::Impl`/`sol::state`) so binding code can
      reach the real Lua state without putting sol2 in any public header.
- [x] Registration API: `register_block` (idempotent by name via new
      `BlockRegistry::add_or_get`, rejected after `freeze()`), `register_item`,
      `register_entity` (captures `on_spawn`/`on_tick`/`on_hit`/`on_death` —
      **nothing dispatches them yet**, waits on 3.1's EnTT registry),
      `register_biome`/`register_craft` (captured, no consumer this phase).
      `worldgen.set_pipeline` **not implemented** (out of scope — worldgen
      pipeline swap deferred, see below).
- [x] Freeze registries after pack load (`PackRuntime::freeze()`); stable
      `BlockId`s via `add_or_get`. **Known gap:** the server registry starts
      from `BlockRegistry::base()` and Lua-added ids beyond it are invisible
      to any client until 4.3 (`S2C_BlockRegistry`) ships — logged as a
      warning, not enforced.
- [x] Runtime world API: `get_block`/`set_block`/`raycast`/`spawn`.
      `raycast` shares one new `vb::world::raycast_voxel()` (extracted from
      `src/client/main.cpp`'s block-selection code, `inc/vb/world/raycast.hpp`)
      with the client. `spawn` logs + returns `nil` (no entity system to spawn
      into). `set_block` **known gap:** doesn't run the relight cascade
      `C2S_BlockEdit` does.
- [x] Entity + player API: one merged Lua usertype (`entity:`/`player:` are
      the same object — no non-player entity exists to justify two types)
      exposing `get_pos`/`set_velocity`/`remove` (no-op, logged)/
      `get_inventory`/`give`/`send_message`/`open_ui`/`get_name`. `send_message`
      / `open_ui` are real wire messages now — see 4.2's protocol addition
      below; no client handles them yet (4.5).
      `ServerSession` gained `set_player_velocity`/`conn_for_player`/
      `player_name` (+ a `name` field on its `Conn`, + `net_id` on
      `SessionPlayerLeft`) to support this.
- [x] Event bus: `vb.on("player_join"|"player_leave"|"block_break"|
      "block_place"|"player_interact"|"chat"|"tick", handler)` with veto
      returns. `player_join` wraps `HandshakeServerHost::authenticate` (a real
      pre-join veto, `install_join_veto`); `block_break`/`block_place` +
      per-block `on_break`/`on_place` fire from a new `WorldReplicator::
      BlockEditHooks` seam at `apply_block_edit` (closes the seam this doc
      called out under 5.2) — `on_break`/`on_place`'s return value (a
      would-be drop) is logged only, not materialized (waits on 5.1 items).
      `chat`/`player_interact` are wired generically
      (`dispatch_chat`/`dispatch_player_interact`) but nothing calls them yet
      — no `C2S_Chat`/interact message exists.
- [x] Scheduling: `vb.after`, `vb.every` (repeating, catches up on a stalled
      tick, capped at 8 fires/dispatch).
- [x] `vb.storage` — metatable-backed proxy over an `nlohmann::json` object
      (new dependency, header-only), persisted to `<content_pack>/storage.json`,
      flushed once per tick when dirty.
- [x] Protocol: `S2C_Chat` (101) + `S2C_OpenUi` (103) now have real codecs
      (`inc/vb/protocol/chat.hpp`) — `kEngineProtocolVersion` bumped 3 → 4,
      `docs/protocol.md` updated. Round-trip tested.
- [x] Unit tests (`tests/unit/pack_runtime_test.cpp`) + integration tests over
      a real `ServerSession`/`ClientSession`/`LoopbackTransport`
      (`tests/unit/pack_runtime_integration_test.cpp`): block-edit veto +
      `on_break` end-to-end, `player_join` veto end-to-end, `player_leave`
      dispatch with the right net id.
- [ ] `EntityKind` tick/spawn/hit/death callbacks wired into `ScriptPreTick` /
      `ScriptPostTick` systems — waits on 3.1's EnTT registry; `register_entity`
      already captures the callbacks, nothing iterates entities to call them.
- [ ] Lua-driven worldgen pipeline replaces the Phase 2 hardcoded one; biomes
      + carvers + decoration — explicitly out of this pass's scope, still a
      separate, meaningfully-sized follow-up.
- [ ] `register_entity`'s `visual = {...}` sub-table (atlas, `facings`,
      per-clip frame lists) for 3.5's `entity_renderer` — not added; nothing
      reads a per-kind visual def yet (3.5 still hardcodes a flat placeholder).

### 4.3 Block registry replication  ✅ (name/solid/opaque/liquid/light only)

- [x] `S2C_BlockRegistry` (40) message (`inc/vb/protocol/world.hpp` +
      `src/protocol/world.cpp`): `varint n` + `n × {name, solid, opaque,
      liquid, light_emission}`, index == `BlockId`. Round-trip tested.
      `kEngineProtocolVersion` bumped 4 → 5. **No model/face-texture/
      collision-shape fields** — `BlockType` doesn't have them yet (waits on
      4.4 asset sync + 5.1 base pack); this phase only replicates what
      already exists.
- [x] Sent between `C2S_Ready` and `S2C_JoinAccept` (spec §8.3 order) via a
      new opt-in `HandshakeServerHost::block_registry` hook
      (`std::optional<vector<BlockRegistryRecord>>`, default `nullopt` = no
      frame sent at all) — every pre-4.3 host/test that never heard of this
      keeps identical wire behavior. `src/server/main.cpp` wires the
      dedicated server's live (possibly Lua-extended) registry into it,
      closing the exact gap `PackRuntime::freeze()` warns about (4.2).
- [x] Client (`ClientSession::tick`, `src/net/session.cpp`) intercepts
      `S2C_BlockRegistry` unconditionally (not through `ClientHandshake`'s
      strict per-state FSM) — robust to it arriving just before or after
      `JoinAccept`, since on real `GnsTransport` it travels on a different
      lane with no cross-lane ordering guarantee. Rebuilds a
      `world::BlockRegistry` in wire order and swaps it into
      `ClientChunkStore` via a new `set_registry()` setter.
- [x] Tests: `tests/unit/protocol_test.cpp` round-trip;
      `tests/unit/block_registry_test.cpp` (new) — a custom registry reaches
      a joined client end-to-end over `LoopbackTransport`, and a host that
      never opts in leaves the client on its own `base()` (regression guard
      for every other session-based test).
- [ ] Client meshing/atlas driven by the received registry beyond
      solid/opaque/light — still just untextured cubes (Phase 2 status);
      waits on a real texture/model concept (4.4/5.1).
- [ ] `src/client/main.cpp`'s singleplayer path doesn't wire this (no
      `PackRuntime` there yet, so its registry is always `base()` anyway —
      not a gap in practice, just unexercised).

### 4.4 Asset Sync Protocol (§9)  ✅ (progress accounting + UI deferred)

- [x] Server startup: walks the pack dir (`vb::assetsync::build_manifest`,
      `inc/vb/assetsync/manifest.hpp` + `src/assetsync/manifest.cpp`),
      rejects `..`/absolute paths/symlinks escaping the root, hashes every
      file (xxHash3-128, `XXH3_128bits`), builds `Manifest` + `manifest_hash`.
      Enforces `ServerConfig::asset_max_file_mb`/`asset_max_total_mb` as a
      hard startup failure, not a per-client thing. Gated behind
      `VB_WITH_COMPRESSION` (disabled build: `kDisabled`, whole feature
      no-ops gracefully like `VB_WITH_NET` off).
      **Dependency landmine fixed:** lz4 vendors its own private, older
      `xxhash.h` with no XXH3 API; `src/core/CMakeLists.txt` now links
      `xxHash::xxhash` *before* `LZ4::lz4` so `#include <xxhash.h>` resolves
      to the real one (see `docs/protocol.md`'s dependency table).
- [x] Messages: `C2S_AssetManifestRequest`, `S2C_AssetManifest`,
      `C2S_AssetRequest`, `S2C_AssetData` (chunked, lane 3) —
      `inc/vb/protocol/assetsync.hpp` + `src/protocol/assetsync.cpp`, round-trip
      tested. `kEngineProtocolVersion` bumped 5 → 6 (a structural handshake
      change, not just additive). Progress accounting (bytes-vs-manifest-total
      for a UI progress bar): not added — no connect-screen UI exists yet
      (waits on 5.3).
- [x] Client content-addressed cache (`vb::assetsync::ClientAssetCache`,
      `inc/vb/assetsync/cache.hpp` + `src/assetsync/cache.cpp`): disk layout
      `<cache_dir>/<2-hex-prefix>/<32-hex-hash>`, hand-rolled `index.bin`
      (write-to-.tmp-then-rename) mapping hash → `{size, last_used}` for LRU
      eviction to `ClientConfig::asset_cache_mb`. Default dir is a new
      `vb::core::user_cache_dir()` (`inc/vb/core/paths.hpp`, OS-appropriate:
      `%LOCALAPPDATA%`/`~/Library/Caches`/`$XDG_CACHE_HOME` or `~/.cache`),
      overridable via a new `ClientConfig::asset_cache_dir` +
      `--asset-cache-dir`.
- [x] Transfer state machine: three new `ServerHandshakeState` values
      (`kAwaitingAssetManifestRequest`/`kAwaitingAssetRequest`/
      `kStreamingAssets`) and two new `ClientHandshakeStatus` values
      (`kAwaitingAssetManifest`/`kSyncingAssets`) sit between `AuthResult`
      and `Ready` unconditionally now. `kStreamingAssets` is server-paced
      (`ServerHandshake::pump_assets()`, called once per tick from
      `ServerSession::tick()`'s existing non-playing-connection loop — a
      small per-tick chunk budget, not literal byte-in-flight windowing).
      Per-file hash verify + abort-on-mismatch/size-cap-breach lives in
      `ClientAssetCache::ingest_chunk` (spec §9.4, wired to `on_asset_chunk`
      returning `false` → `ClientHandshake::fail()`).
- [x] Virtual pack filesystem in memory (`ClientAssetCache::virtual_fs()`,
      exposed on `ClientSession` as `virtual_pack_fs()`) — assembled from
      cache reads + completed transfers. **Nothing consumes it yet**: Lua
      `require` (4.1), texture/model/UI loaders don't exist regardless of
      this landing.
- [x] Handshake ordering: `AssetManifestRequest → AssetManifest →
      AssetRequest → AssetData×N` sits between `AuthResult` and `Ready`
      (before `S2C_BlockRegistry`/`JoinAccept`, confirmed against the §8.3
      diagram) — not "before BlockRegistry/JoinAccept" loosely, exactly
      between Auth and Ready as spec'd.
- [x] Reconnect fast-path — **scoped down, confirmed with the user**: not
      persisted across process restarts. `C2S_AssetManifestRequest.
      known_manifest_hash` lets a *same-process* reconnect skip the manifest
      listing too, but a restarted client always gets the full listing —
      its on-disk CAS still makes `compute_missing()` return empty and the
      *data transfer* zero-byte regardless, which is what the integration
      test actually asserts. A literal cross-restart `manifest_hash` skip
      (persisting `HandshakeClientHost::last_known_manifest_hash`'s value to
      disk) is a cheap, unimplemented follow-up if wanted.
- [x] Integration test (`tests/unit/netcode_test.cpp`): a cold client
      downloads a real temp content pack and ends up byte-identical
      (`virtual_pack_fs()` vs. on-disk source); a second connection (fresh
      `ClientAssetCache` instance, same cache dir — simulating a process
      restart) asserts the server's `asset_file_bytes` hook is never called.
      Plus dedicated unit tests: `tests/unit/assetsync_manifest_test.cpp`
      (determinism, content-sensitivity, symlink-escape rejection, size
      caps) and `tests/unit/assetsync_cache_test.cpp` (missing-hash
      computation, hash-mismatch rejection leaves no stray file, LRU
      eviction under a tiny cap).

### 4.5 Client UI VM + raygui (§10.4)  ✅ (item grid + base pack deferred)

- [x] Separate restricted client VM: `vb::script::UiRuntime`
      (`inc/vb/script/ui_runtime.hpp` + `src/script/ui_runtime.cpp`) — a
      second `Vm` distinct from the server's `PackRuntime`, pImpl'd the same
      way, disabled-stub when `VB_WITH_LUA` is off. No world/net access of
      its own; outgoing events route through an attached `ClientSession&`.
- [x] `ui.define(name, layout_fn)` declarative layout: `label`, `panel`,
      `button`, `list`, `text input` (spec's set minus **item grid**,
      deferred — needs real items, waits on 5.1). Widgets are computed once
      at `open()` and don't re-layout afterward (documented limitation).
- [x] C++ renderer mapping layout → raygui: `vb::render::UiRenderer`
      (`inc/vb/render/ui_renderer.hpp` + `src/render/ui_renderer.cpp`),
      split across the core/render boundary from `UiRuntime` the same way
      `ClientChunkStore`/`ChunkRenderer` already are — `UiRuntime` emits a
      plain `Widget` list (no raygui), `UiRenderer` draws it (no sol2) and
      reports back which widgets fired an interaction this frame.
- [x] `player:open_ui(name, ctx)` server→client: `ClientSession` gained
      `take_open_ui()` (drains a pending `S2C_OpenUi`) + `send_ui_event()`.
      `on_click`/`on_change`/`on_close` → `ui.send_event(...)`/`ui.close()`
      → real `C2S_UiEvent{ui_name, widget_id, event_kind, value_json}`
      (`inc/vb/protocol/chat.hpp`, round-trip tested) →
      `ServerSession::set_ui_event_handler` (a plain `std::function`, no
      script dependency — same shape as `WorldReplicator::BlockEditHooks`)
      → `PackRuntime::dispatch_ui_event` → `vb.on("ui_event", handler)`
      (non-vetoable; spec gives no veto semantics for UI events).
      `kEngineProtocolVersion` bumped 6 → 7.
- [x] `src/client/main.cpp` wired end to end: `UiRuntime`/`UiRenderer`
      constructed unconditionally, drawn each frame between the 3D pass and
      `window.end_frame()`, mouse capture released while a UI is open.
      **No base pack (5.1) exists yet**, so nothing calls `ui.define` at
      real runtime today — same shipped-mechanism-ahead-of-content pattern
      as every prior 4.x phase.
- [x] Tests: `tests/unit/protocol_test.cpp` (`C2SUiEvent` round-trip),
      `tests/unit/ui_runtime_test.cpp` (layout evaluation, undefined-name
      no-op, `on_click`/`on_close` invocation), and a full end-to-end
      integration test in `tests/unit/pack_runtime_integration_test.cpp`:
      a server pack script's `block_break` handler calls `player:open_ui`
      (no earlier event hands a script a `PlayerHandle` for an online
      player), the client's `UiRuntime` opens it and a simulated button
      click round-trips to `vb.on("ui_event", ...)` on the server with the
      right `ui_name`/`widget_id`/`kind`/`value`.
- [ ] Base pack `ui/inventory.lua`, `ui/pause.lua` — waits on 5.1 (no
      content pack exists to put them in).
- [ ] Item grid widget — waits on 5.1 (needs a real item/inventory concept).

**Phase 4 exit — met (mechanism, not content):** all five Lua/asset-sync
subsystems (4.1–4.5) are implemented and tested end-to-end over
`LoopbackTransport`. What's still missing to literally satisfy the exit
line ("server runs the base Lua pack ... opens a Lua-defined inventory
screen") is content, not mechanism: there is no `content/base` pack yet
(Phase 5.1) for a real server to load, so nothing calls `vb.register_block`/
`ui.define`/etc. outside of tests today.

---

## Phase 5 — Minimum Playable Base

Goal: a small, coherent, playable multiplayer sandbox.

### 5.1 Base content pack (`content/base`)  🚧 (registration + wiring done; art/inventory-sync deferred)

- [x] `pack.toml`, `init.lua`. **`entry` field is not actually read** — real
      `require` still doesn't exist (4.1), so a pack's `init.lua` can't pull
      in its own `blocks/*.lua` itself. New `vb::script::load_content_pack`
      (`inc/vb/script/pack_loader.hpp` + `src/script/pack_loader.cpp`) is the
      substitute: the *host* walks `blocks/*.lua` (sorted) → `entities/*.lua`
      (sorted) → `biomes/*.lua` (sorted) → `init.lua`, loading each as its
      own chunk into the same `PackRuntime`/Lua state — behaviourally one
      concatenated script, since every file shares `vb.register_*`'s globals.
      `pack.toml`'s `entry` is kept only as the spec-documented (§16) field
      for whenever real `require` lands.
- [x] **Closed a real gap, not just content:** neither binary ever loaded a
      pack before this — `src/server/main.cpp` constructed a `PackRuntime`
      and immediately `freeze()`d it with nothing registered (every Phase 4.2
      test exercised `PackRuntime` directly, never through the server's own
      main). Now wired: `load_content_pack(pack_runtime, config.content_pack)`
      before `freeze()`, fatal-erroring the server on a real pack syntax/
      runtime error (same "broken pack is fatal" pattern as a bad asset
      manifest), non-fatal + once-logged on a `VB_WITH_LUA`-off build.
- [x] Blocks: dirt, grass, wood, leaves, stone, sand — `blocks/*.lua`, each
      `vb.register_block{...}` **re-declaring the exact name** the Phase 2
      hardcoded `BlockRegistry::base()` (`src/world/block.cpp`) already used
      (`add_or_get` is idempotent by name), so ids are unchanged unless the
      pack adds something new. `on_break` gives the broken block back to the
      breaking player via `player:give(...)` — a real, working drop (not the
      still-unmaterialized `on_break` *return-value* path
      `pack_runtime.cpp`'s `on_block_edit_after` only logs). `blocks/grass.lua`
      drops dirt (`base_dirt_id`, a plain Lua global blocks/dirt.lua sets —
      the cross-file-shared-globals mechanism the loader above relies on).
      **No textures** — nothing in the client consumes a per-block texture
      yet (4.3's known gap: "still just untextured cubes"), so no `.png`
      files were added; would be inert weight until a texture/atlas loader
      exists.
- [~] Biome(s): `biomes/plains.lua` / `biomes/forest.lua` call
      `vb.register_biome{...}` with surface/filler/stone (+ a `decoration`
      hint on forest) — **purely declarative**, same as `register_biome`
      already was: nothing reads a biome back (`WorldGenerator` is still the
      Phase 2 hardcoded fBm pipeline, `vb.worldgen.set_pipeline` doesn't
      exist). No tree decoration — the worldgen decoration pass itself is
      unbuilt (2.2).
- [x] Client UI screens wired end-to-end for real multiplayer:
      `src/client/main.cpp` now loads every synced `ui/*.lua` file from
      `ClientSession::virtual_pack_fs()` (Asset Sync, 4.4) into `UiRuntime`
      right after join — the same "mechanism shipped, nothing called it"
      gap 4.5 flagged is closed for *loading*. `ui/pause.lua` and
      `ui/inventory.lua` (`base:pause` / `base:inventory`) are real,
      loadable `ui.define` screens.
      **Known gap, not attempted:** nothing can *open* either screen in
      real gameplay yet — `player:open_ui` is server-push-only and there is
      no client gesture or C2S message requesting "open my inventory" /
      "pause" (5.4 tracks chat/interact messages generally; an open-UI
      request is the same shape of gap). `ui/inventory.lua` also renders
      numeric item ids, not names (`vb.register_item` never allocates its
      own id space — see below), and uses a `list` widget in place of the
      spec's item-grid (still doesn't exist, 4.5).
      **`--singleplayer` still doesn't asset-sync at all** (no
      `PackRuntime`/manifest on that in-process path — REMAINING_TASKS.md
      4.3's pre-existing note), so this new loading loop only ever finds
      files for real multiplayer (`--server`) connections, unchanged from
      before.
- [ ] Player + dropped-item billboard sprite atlases (§11.3 / 3.5) — replaces the
      Phase 3 flat-placeholder quad with real directional art. Not started.
- [ ] Dropped-item entity — `entities/dropped_item.lua` registers
      `base:dropped_item` via `vb.register_entity` (declarative only, same
      "nothing dispatches spawn/tick" gap as `register_biome` — waits on
      3.1's EnTT registry); real drops in this pack go straight into the
      breaking player's inventory (see blocks/*.lua above) instead of
      spawning a world entity.
- [ ] Real inventory: `entity:get_inventory()`/`player:give()` (4.2) work and
      are used by `blocks/*.lua`'s `on_break`, but there is **no wire
      message syncing inventory contents to the client at all** — it's
      server-Lua-only state. `ui/inventory.lua` can only show a snapshot
      handed to it explicitly via `player:open_ui("base:inventory", {slots =
      player:get_inventory()})` from a script, not anything live. A real
      hotbar needs this closed first.
- [ ] Basic hotbar — client HUD, waits on the inventory-sync gap above.
- [ ] Simple crafting recipes (wood → planks → sticks, etc.) — optional, not
      attempted; `vb.register_craft` remains captured with zero consumer.
- [x] New regression coverage: `tests/unit/content_pack_test.cpp` loads the
      *real* `content/base` files (not inline Lua strings, unlike every
      other pack_runtime test) through `load_content_pack`, asserting they
      parse/register cleanly and re-declare the base block ids unchanged;
      plus a broken-pack-file-is-fatal case. Nothing else in the suite
      exercises the shipped files themselves.

### 5.2 Block breaking / placing over the network (§8.5)  🚧

- [x] `C2S_BlockEdit` (break/place, `predicted_seq`, world `pos`, `block_id`) +
      `S2C_BlockEditResult` — `vb/protocol/world.{hpp,cpp}`, round-trip tested.
      Proto version 2 → 3.
- [x] Client optimistic apply + rollback on reject — `ClientSession::push_block_edit`
      / `handle_block_edit_result`; `ClientChunkStore::edit_block` re-meshes the
      chunk + its border neighbours.
- [x] Server validation: reach (≤5.5 m), target validity, non-floating placement
      — `WorldReplicator::apply_block_edit`. **Lua veto + region protection: seam
      left, waits on Phase 4.2.** Tool/hardness times: not yet.
- [x] Apply + bump `revision` + dirty light/mesh + whole-chunk `relight_chunk`.
      `on_break`/`on_place` callbacks + drops: waits on Phase 4.2 / items (5.1).
- [x] `S2C_BlockEditResult` to the editor + `S2C_ChunkDelta` (block + diffed
      light) fan-out to every player mirroring the chunk.
- [~] Relight on edit: per-chunk from scratch each edit; cross-chunk propagation
      (breaking a floor lets light into the chunk below) still TODO.
- [x] Selection raycast (Amanatides–Woo) + wire-cube highlight; LMB break /
      RMB place stone. Break *progress* (hold-to-break): not yet.

### 5.2 status (2026-09-11): playable over loopback. `voxel_browser --singleplayer`
can break and place blocks; a second client sees the change via `S2C_ChunkDelta`;
out-of-reach edits roll back. Remaining: Lua veto (4.2), drops/tools/items (5.1),
hold-to-break progress, cross-chunk relight.

### 5.3 Main menu (raygui, engine-level, not pack)  ✅ (keybindings screen deferred)

- [x] `vb::render::MainMenu` (`inc/vb/render/main_menu.hpp` +
      `src/render/main_menu.cpp`, new `vb_render` sibling to `ChunkRenderer`/
      `UiRenderer` — plain raygui calls, no sol2/Lua, no `ClientSession`
      dependency of its own) draws four screens; `src/client/main.cpp` was
      restructured around an explicit `AppState{kMenu, kSettings,
      kConnecting, kPlaying, kError}` state machine that owns which of
      `Singleplayer`/`RemoteConnection` is alive. The window now opens
      *before* any connection attempt in windowed mode (previously
      `main.cpp` blocked on a full connect+handshake before ever creating a
      `Window`).
- [x] Main screen: player name + server address/port text fields (raygui
      `GuiTextBox`/`GuiValueBox`, same edit-mode-toggle pattern
      `UiRenderer::draw`'s `kTextBox` case already used), **Connect**,
      **Play Singleplayer**, **Settings**, **Quit**.
- [x] Connecting/progress screen: shows the live `ClientHandshakeStatus`
      as text (`connecting_status_text()` in `main.cpp` — Connecting /
      Authenticating / Requesting content manifest / Downloading content
      pack / Syncing world) + a **Cancel** button that tears down the
      in-flight `Singleplayer`/`RemoteConnection` and returns to the menu.
      **No byte-progress bar** — `ClientHandshake`/asset-sync (4.4) never
      grew progress-fraction accounting (4.4's own known gap: "no
      connect-screen UI exists yet" — now one does, but the underlying
      counter still doesn't), so this is status-text-only, not a filled bar.
      Real multiplayer connects are pumped one `tick(dt)` per frame with a
      10 s wall-clock deadline (was a blocking `sleep`-based loop before the
      window existed); singleplayer's loopback join is ticked in small
      batches per frame (was a single blocking up-to-128-tick loop) since
      it's synchronous/in-process and finishes in a handful of frames
      regardless.
- [x] Error screen: shows `ClientSession::failure_reason()` (or "connection
      timed out" / a pre-handshake connect failure), **Back to menu** button
      — connect failures no longer exit the process in windowed mode (they
      used to: the old code `return EXIT_FAILURE`d straight out of `main`).
- [x] Recent servers list: `ClientConfig::recent_servers` (already existed
      as an unused field, `client.toml`'s documented `recent_servers = []`)
      is now actually read/written — a `GuiListView` on the main screen
      fills the address/port fields on click; a successful non-singleplayer
      join pushes `"host:port"` to the front (dedup, capped at 8) and
      persists via a new `vb::core::save_client_config()`
      (`inc/vb/core/config.hpp` + `src/core/config.cpp`, toml++ serializer —
      regenerates the file from scratch, doesn't preserve
      `client.toml.example`-style comments in a real `client.toml`).
- [x] Settings screen: window width/height (persisted for next launch, not
      live-resized — labelled "applies on restart"), vsync (same), FOV /
      render distance / mouse sensitivity (`GuiSlider`), asset cache MB
      (`GuiValueBox`); **Save** writes through `save_client_config` and
      returns to the menu, **Back** discards edits.
      **Keybindings: not attempted** — WASD/jump/sprint/break/place are
      still hardcoded in `sample_input_cmd()`/the block-edit block in
      `main.cpp`; out of scope for this pass, no rebinding storage or UI
      exists.
- [x] Integrated-server singleplayer path: unchanged mechanism from Phase 1
      (`Singleplayer` struct, in-process `IntegratedGame` over
      `LoopbackTransport`) — now reachable from the **Play Singleplayer**
      button instead of only `--singleplayer` on the CLI.
- [x] `--headless` deliberately untouched: `run_headless()` in `main.cpp` is
      the pre-5.3 blocking connect-then-run body, byte-for-byte behaviourally
      unchanged, so `server_smoke`/`client_smoke`/`singleplayer_smoke` (CI's
      only coverage of this file) keep passing without modification — the
      new menu code is only reachable in windowed mode. `--singleplayer` or
      an explicit `--server` on the CLI in *windowed* mode skips the menu
      and calls the same `begin_connect()` the Connect/Play Singleplayer
      buttons use, landing on the Connecting/Error screens instead of
      exiting the process on failure (a behavior change from before, judged
      strictly better: a bad `--server` used to hard-exit).
- [ ] Not covered by an automated test (no windowed GL context in CI/tests,
      same limitation as `ChunkRenderer`) — verified by: a clean
      `-DVB_WARNINGS_AS_ERRORS=ON` build across `voxel_browser`/
      `voxel_browser_server`/`vb_tests`, the full `ctest` suite green
      (`vb_tests` unaffected — nothing in `vb_core`/`vb_render`'s public
      surface changed shape besides the additive `MainMenu`/
      `save_client_config`), and a real windowed launch + screenshot showing
      the main menu laid out correctly (player name / address / port /
      Connect / Play Singleplayer / Settings / Quit). Clicking through
      Settings/Connecting/Error wasn't exercised interactively this session
      (an automated-input attempt via a background PowerShell couldn't
      reliably focus the raylib window to deliver clicks — a host/tooling
      limitation, not a sign of an app bug) — worth a manual pass before
      calling 5.3 fully verified.

### 5.4 Play polish

- [x] Day/night `time_of_day` from `JoinAccept`, advanced server-side, simple sky
      gradient client-side: `S2C_TimeOfDay` (46) — `inc/vb/protocol/world.hpp`
      + `src/protocol/world.cpp`, round-trip tested. `kEngineProtocolVersion`
      bumped 9 → 10. `S2C_JoinAccept::time_of_day` existed since Phase 1.3
      but nothing ever advanced it or kept an already-connected client in
      sync — closed both gaps: new pure (no raylib) `vb::world::daynight.hpp`
      + `.cpp` (`kTicksPerDay = 24000`, `advance_time_of_day`,
      `sky_brightness`, `sky_color_for_time`; unit-tested,
      `tests/unit/daynight_test.cpp`), `ServerSession` advances its own
      `time_of_day` every tick (`set_day_length_seconds`, default
      1200s/day == 20 real minutes), fills a joining player's `JoinGrant
      ::time_of_day` from it, and broadcasts `S2C_TimeOfDay` to every playing
      connection about once a second (coarser than snapshots — the clock
      only needs to look smooth). `ClientSession::time_of_day()` returns the
      join-time value until the first update lands, then tracks the latest
      broadcast. `src/client/main.cpp`: `ClearBackground` behind the 3D view
      uses `sky_color_for_time(client->time_of_day())` (a 4-keyframe
      sunrise/noon/sunset/midnight gradient, not a physically based sky),
      plus an "HH:MM" readout added to the existing debug overlay. Tests:
      `tests/unit/protocol_test.cpp` (round-trip),
      `tests/unit/daynight_test.cpp` (rate/wrap/brightness/color math), and a
      new `tests/unit/netcode_test.cpp` case (sped-up day length; asserts a
      joined client's clock advances past its join-time value via the
      periodic broadcast, and a second client joining later gets a later
      `JoinAccept.time_of_day` than the first — proving the grant is
      live-filled per join, not fixed at server startup). Full `ctest` green
      (4/4) on `build-asan-nonet`; clean `-DVB_WARNINGS_AS_ERRORS=ON` build
      of all three targets.
      Not attempted: no ambient-light/mob-spawning gameplay coupling (purely
      cosmetic this pass); no ability for a pack to set/override the day
      length or a specific starting time_of_day (`set_day_length_seconds` is
      C++-only, no `vb.` Lua binding); the sky gradient is a flat
      `ClearBackground` fill, not a real skybox/sun/moon/star render.
- [x] Chat: `C2S_Chat` (100) + `S2C_Chat` (101) — `inc/vb/protocol/chat.hpp` +
      `src/protocol/chat.cpp`, round-trip tested. `kEngineProtocolVersion`
      bumped 7 → 8. `ServerSession::set_chat_handler` (mirrors
      `set_ui_event_handler`'s shape) routes a playing connection's
      `C2S_Chat` to an optional `bool(NetId, string_view)` veto before
      `ServerSession` itself formats `"<name>: <text>"` and broadcasts
      `S2C_Chat` to every playing connection (sender included) —
      `PackRuntime::attach_session` wires it to the already-existing
      `dispatch_chat`/`vb.on("chat", ...)` seam (was captured but never
      reachable — `C2S_Chat` didn't exist yet). No handler set (e.g.
      `--singleplayer`, no `PackRuntime`) = default-allow, chat still works
      without a pack. `ClientSession::send_chat`/`take_chat_messages()`.
      `src/client/main.cpp`: a small HUD chat box (`kPlaying` state only) —
      Enter opens a `GuiTextBox` (plain raygui, no Lua, same posture as
      `MainMenu`) and releases mouse capture like an open pack UI does,
      Enter again sends + closes, Escape cancels; a bottom-left scrolling
      log (last 8 lines) shows incoming `S2C_Chat`. Tests:
      `tests/unit/protocol_test.cpp` (round-trip),
      `tests/unit/netcode_test.cpp` ("chat: a broadcast reaches every
      playing client, including the sender" — also asserts an empty line
      is dropped server-side, not broadcast), and
      `tests/unit/pack_runtime_integration_test.cpp` ("pack script vetoes
      chat from a specific player" — end-to-end over `LoopbackTransport`).
      Not attempted: rate limiting / flood guard, `/`-prefixed commands,
      per-message timestamps, chat history persisted across a reconnect.
- [x] Player list / join-leave messages: `S2C_PlayerJoin` (104) /
      `S2C_PlayerLeave` (105) / `S2C_PlayerList` (106) —
      `inc/vb/protocol/chat.hpp` + `src/protocol/chat.cpp`, round-trip tested.
      `kEngineProtocolVersion` bumped 8 → 9. `ServerSession` generates these
      itself (no Lua involvement, same posture as chat's server-side
      formatting): a freshly-joined connection gets one `S2CPlayerList` of
      everyone else already playing, then every other playing connection
      gets `S2CPlayerJoin`; a disconnect broadcasts `S2CPlayerLeave` to
      everyone remaining. `ClientSession::players()` keeps a live
      `net_id -> name` map from these; join/leave also land as
      `"* <name> joined/left the game"` lines through the existing
      `take_chat_messages()` seam (5.4's chat log), so no second HUD widget
      was needed for that half. `src/client/main.cpp` draws a small
      always-visible player list, top-right (own name highlighted, no toggle
      key -- avoids clashing with Tab, already bound to mouse-capture
      release). Tests: `tests/unit/protocol_test.cpp` (round-trip) and a new
      `tests/unit/netcode_test.cpp` case asserting a newcomer's player list
      contains the existing player, the existing player gets the join
      broadcast + system chat line, and both clear on disconnect. Also fixed
      two pre-existing chat tests that joined two clients simultaneously and
      didn't drain the now-also-arriving join system line before asserting
      on `take_chat_messages()`.
      Not attempted: player list persists no extra metadata (ping, idle
      time); no distinct "system message" channel from real chat (join/leave
      share the same log/take_chat_messages() stream, colour-coded only by
      the "* " prefix).
- [ ] Death/respawn (fall out of world, `Health` at 0) with spawn point.
- [ ] Basic sfx hooks are stubbed (no audio subsystem in v0) — document.

### 5.5 Documentation

- [ ] `docs/lua-api.md`: complete, example-driven reference for every API in
      §10.3–10.4, generated stubs + hand-written prose.
- [ ] `docs/protocol.md`: every message, every lane, the handshake, versioning.
- [ ] Finalize README "Getting Started": real build steps per platform incl.
      GameNetworkingSockets system deps; how to run a server + connect.
- [ ] `content/base` acts as the tutorial pack — comment it heavily.
- [ ] `CONTRIBUTING.md`: module map, build options, how to run tests.

**Phase 5 exit:** build from source on all 3 platforms; run a server with the
base pack; two players connect, mine and place blocks, see each other, chat, and
open the inventory UI. Nothing gameplay-facing is hardcoded in C++.

---

## Cross-Cutting / Continuous

- [ ] Keep `ENGINE_PROTOCOL_VERSION` + `docs/protocol.md` in lockstep with every
      wire change.
- [ ] Every new `vb/protocol` struct gets a round-trip + fuzz test.
- [ ] Sanitizer (ASan/UBSan) debug CI job; TSan job for the threaded subsystems.
- [ ] Determinism golden-value CI gate stays green across platforms.
- [ ] Soak test target (N simulated clients, random walk + edits) run nightly or
      pre-release; watch queue growth + leaks.
- [ ] Perf budget checks: chunk mesh time, snapshot size, frame time — track in a
      simple benchmark harness.
- [ ] `--headless` stays functional for both binaries (CI + integration tests).
- [ ] Address the 6 open questions in `ARCHITECTURE_SPEC.md` §19 as their blocking
      phase arrives; record decisions in that section.

---

## Deferred (post first-playable)

- World persistence: region file format, save/load, chunk eviction to disk.
- Account/auth token verification service (`auth_mode = token`).
- Audio subsystem + Lua sfx/music API.
- Server-side plugin hot-reload.
- Entity–entity physics, mounts, projectiles beyond basics.
- Client-side particle system beyond block-break puffs.
- Compression tuning (zstd), snapshot delta compression, bit-packed inputs.
- Dedicated server browser / master server list.
- Modding: multiple stacked content packs, dependency resolution.
