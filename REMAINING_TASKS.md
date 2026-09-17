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
- [x] Wire real librg in (`VB_WITH_REPLICATION`) as the interest backend —
      `InterestGrid::visible_from` now dispatches to a librg-backed
      implementation (`src/replication/interest.cpp`) at compile time; same
      public interface, same diff semantics, no caller changes. CI now
      builds with `-DVB_WITH_REPLICATION=ON` on all three OSes.
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
- [ ] Biome selection (2.2 step 2) + carvers + vein/scatter + decoration pass
      — deferred to the Lua pipeline (Phase 4); base pipeline is
      heightmap-only for now. Biome selection design updated 2026-09-17 to
      Voronoi-cell partitioning with adjacency-weighted probability
      (WFC-flavored, non-backtracking — see `ARCHITECTURE_SPEC.md` §6 stage
      2), not the originally-sketched continuous temperature/humidity noise.

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
- [x] Fixed 20 Hz tick loop with accumulator (2026-09-17): a dedicated server
      is naturally paced at `tick_rate` by `sleep_until()` (`src/server/main.cpp`),
      but `--singleplayer`'s integrated server (`Singleplayer::tick()`,
      `src/client/main.cpp`) was stepping the server once per render frame
      with the raw frame `dt` — authoritative sim rate (and therefore physics/
      worldgen determinism, replication cadence) depended on framerate, unlike
      every other server. `Singleplayer::tick()` now accumulates frame `dt`
      and steps `server.tick()` + the pack runtime's join/leave/tick dispatch
      at a fixed `1/20 s`, capped at 5 catch-up steps per frame (drops the
      backlog past that rather than spiralling). Client-side prediction still
      ticks once per real frame, unchanged.
- [x] EnTT registry wiring on the server (2026-09-17): `ServerSession` now
      creates a real entity per playing connection (`Conn::entity`) holding
      `ecs::Position/Velocity/Rotation/Collider/PlayerInput/Health/PlayerTag/
      NetReplicated`, populated on join completion and destroyed on
      disconnect. `Conn`'s old inline fields (`move`, `look`, `name`, `health`,
      `last_input_seq`) are gone -- every method that used to read/write them
      (`handle_input_batch`, `handle_chat`, `check_respawns`,
      `broadcast_snapshots`, `player_move_state`, `set_player_velocity`,
      `player_name`) now goes through `registry_.get<...>()` directly, so the
      registry is the actual source of truth, not a synced mirror.
      `player_move_state()`'s signature changed from a raw pointer to
      `std::optional<physics::MoveState>` (assembled from three components on
      demand, so there's no `MoveState` object to point into anymore) --
      updated its 3 call sites (`pack_runtime.cpp`, `netcode_test.cpp` x2).
      **Still deferred:** nothing iterates the registry generically yet — a
      `SystemRunner` (below) is only worth adding once a Lua entity kind
      (Phase 4) needs to.
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
- [x] Map players ↔ librg network entities — `InterestGrid::upsert`/`remove` track
      every `NetId` (players and item drops alike) 1:1 as a self-owned librg
      entity; see `src/replication/interest.cpp`.

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
      `register_biome`/`register_craft` (captured; the engine itself still
      has no consumer, but as of 5.1 `content/base/crafting.lua` builds a
      real, working crafting feature on top of `register_craft` + the
      generic `player:give()`/`take()` primitives — see 5.1 below).
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
      (Voronoi-cell, adjacency-weighted selection — `ARCHITECTURE_SPEC.md` §6
      stage 2) + carvers + vein/scatter (new stage 5, ore/valuable-block
      placement) + decoration — explicitly out of this pass's scope, still a
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
      **`--singleplayer` still doesn't asset-sync** (no manifest/
      `ClientAssetCache` on the loopback path), so `ui/*.lua` specifically
      still only loads for real multiplayer connections — **but
      `--singleplayer` now runs a real server-side `PackRuntime`** (fixed
      2026-09-16, see 5.1's own entry below), so this gap is now scoped
      down to "no synced client-side asset files," not "no scripting at
      all" the way it read before.
- [ ] Player + dropped-item billboard sprite atlases (§11.3 / 3.5) — replaces the
      Phase 3 flat-placeholder quad with real directional art. Not started.
- [x] Dropped-item entity (2026-09-16): a real, working world item drop —
      `vb::world::ItemDropSystem` (`inc/vb/world/item_drops.hpp` +
      `src/world/item_drops.cpp`), pure/no net dependency, unit-tested
      standalone (`tests/unit/item_drops_test.cpp`: id-space separation from
      player NetIds, pickup-radius collection, lifetime despawn, multiple
      independent drops). `ServerSession::spawn_item_drop(pos, item, count)`
      allocates one and upserts it into the *same* `InterestGrid` a player
      already lives in (`kItemDropKind` sentinel, `EntityKindId{0xFFFF}`) —
      no new wire message needed at all: it replicates through the existing
      `S2C_EntitySnapshot` path, and the client's Phase 3.5 `EntityRenderer`
      already draws any remote entity generically (nothing in it branches on
      `kind`), so a drop just shows up as a tinted billboard for free.
      `ServerSession::update_item_drops()` (called once per tick) ages every
      drop, checks it against every playing connection's *current* interest
      position (works whether that position came from real input-driven
      movement or the test/script-facing `set_player_state`), and reports
      pickups + removals for the caller to apply — `PackRuntime::
      attach_session` wires the pickup handler straight to
      `inventories[player]` + `sync_inventory()` (5.1's real-inventory work,
      above), so picking an item up looks identical to a script calling
      `player:give()`. New Lua binding `vb.world.spawn_item_drop(pos, item,
      count)` (`world_tbl`, `src/script/pack_runtime.cpp`) — deliberately its
      own binding, not routed through the still-inert generic
      `vb.world.spawn`/`vb.register_entity` path (that still waits on 3.1's
      EnTT registry; `entities/dropped_item.lua`'s registration is unchanged,
      kept as the pack-format placeholder for when a script wants custom
      per-drop behavior). `content/base/blocks/*.lua`'s six `on_break`
      handlers now call `vb.world.spawn_item_drop` at the broken block's
      position instead of `ctx.player:give()` directly — breaking a block
      drops a real, visible, walk-over-to-collect item in the world instead
      of an instant inventory credit. Integration test
      (`pack_runtime_integration_test.cpp`, "vb.world.spawn_item_drop
      replicates to a client and is picked up on approach"): a real
      `ServerSession`/`ClientSession` pair over `LoopbackTransport` — spawns
      a drop far from the player (not yet visible), moves the player into
      interest range but short of pickup radius (visible, not collected),
      then onto it (collected: entity disappears from `remote_entities()`,
      `client.inventory()` gains the stack). `content_pack_test.cpp` confirms
      the real edited `content/base` files still parse/register cleanly.
      Verified across `build-lua` (full `vb_tests`, 201/201) and
      `build-asan-nonet` (`VB_WITH_LUA=OFF`, confirming the Lua-gated code
      compiles out cleanly; clean build of all three targets + full `ctest`).
      **Not attempted:** no physics on a drop (it sits exactly where it
      spawned — no gravity/settling, no bounce); no visual distinction
      between drops or from a player (same flat hardcoded billboard 3.5
      already ships, tinted only by a NetId hash — real per-item icons wait
      on textures, same long-standing 4.3/5.1 gap); no stacking/merging of
      nearby same-item drops; no despawn warning/blink before the 120s
      lifetime expires.
- [x] Real inventory (2026-09-16): `S2C_Inventory` (107) —
      `inc/vb/protocol/inventory.hpp` + `src/protocol/inventory.cpp`,
      round-trip tested. `kEngineProtocolVersion` bumped 10 → 11. Closes the
      "no wire message syncing inventory contents to the client at all" gap:
      `PackRuntime::Impl::sync_inventory` pushes a full slot snapshot to a
      player's connection after every `player:give()` (the only mutator that
      exists); `ClientSession::inventory()` holds the latest copy
      (`src/net/session.cpp`'s `apply_gameplay_frame` applies it, same
      posture as `S2C_PlayerList`: always a full resend, no delta-tracking
      machinery for a handful of slots). `ui/inventory.lua`'s one-shot
      `player:open_ui(..., {slots = player:get_inventory()})` snapshot is
      unaffected/still works; this adds a second, always-live path a hotbar
      can read without a script pushing anything. Tests:
      `tests/unit/protocol_test.cpp` (round-trip) and a new
      `tests/unit/pack_runtime_integration_test.cpp` case ("player:give()
      pushes a live S2C_Inventory to the client") driving a real
      `ServerSession`/`ClientSession` pair over `LoopbackTransport`. Not
      attempted: no C++/Lua API to *remove*/consume a stack (nothing needs
      it yet — crafting/consumption are separately unbuilt), no slot-index
      addressing (append-only `slots` vector, same shape `get_inventory()`
      already exposed).
- [x] Basic hotbar (2026-09-16): `src/client/main.cpp` draws
      `client->inventory()` bottom-center, one box per slot, block name (via
      `chunk_store().registry().get(item).name`) + count as plain text — no
      slot-select input, no icons/atlas (waits on real per-block textures,
      4.3/5.1's own still-open gap), same text-only posture
      `ui/inventory.lua` already had. Not visually verified against a live
      window this session (no GL context available here — same limitation
      noted throughout 5.3/5.4); rests on a clean
      `-DVB_WARNINGS_AS_ERRORS=ON` build of `voxel_browser` + the full
      `ctest` suite green.
- [x] Simple crafting recipes (2026-09-16): wood → planks → sticks, exactly
      the example this line originally suggested — and, per explicit user
      direction, implemented **entirely as content, not engine**: the only
      C++ addition is one generic, game-agnostic primitive,
      `player:take(itemstack) -> bool` (`PlayerHandle::take`,
      `src/script/pack_runtime.cpp` — the symmetric counterpart to the
      already-existing `give()`, removing up to `count` of an item across
      however many slots hold it, all-or-nothing, no partial consumption).
      Every actual game rule — what a recipe is, which recipes exist, how a
      player triggers one, what happens on success/failure — lives in a new
      `content/base/crafting.lua`, loaded as a generic top-level pack module
      (see the `load_content_pack` change below). The engine still has zero
      concept of "crafting"; `vb.register_craft` remains a write-only
      registration call as far as C++ is concerned (nothing in
      `src/script/pack_runtime.cpp` reads the `crafts` vector back) — the
      recipe *data* the working feature actually reads lives in a plain Lua
      table inside `crafting.lua` itself, with `vb.register_craft` called
      alongside purely so the engine-side record exists for whenever a real
      consumer (a crafting-table UI's item grid, 4.5's still-missing widget)
      wants it.
      **Generic engine addition #2:** `src/script/pack_loader.cpp`'s
      `load_content_pack` now also loads any other `*.lua` file sitting
      directly at the pack root (sorted, excluding `init.lua`) after
      `blocks/`/`entities`/`biomes/` but before `init.lua` — this is what
      lets `crafting.lua` exist at all without engine code knowing anything
      about crafting specifically; a pack could just as easily drop an
      unrelated `weather.lua` or `economy.lua` there and it would load the
      same way. Purely additive: no existing pack had any other root-level
      `.lua` file, so no other pack's load order changes.
      **Content:** two new craftable-only blocks,
      `content/base/blocks/planks.lua` (solid) and `blocks/sticks.lua`
      (non-solid — the closest approximation to "not a real building
      block" available today, since held items and placeable blocks still
      share one `BlockId` space, see 5.1's own "`vb.register_item` never
      allocates its own id space" gap, unaffected by this change).
      `crafting.lua` triggers off chat: `/craft <name>` checks the
      player's summed inventory against the recipe's inputs
      (`player:get_inventory()`, already existed), `take()`s each input only
      if every one is confirmed sufficient first (all-or-nothing at the
      recipe level too, not just per-`take()` call), then `give()`s the
      output — vetoing (not broadcasting) any `/craft `-prefixed message
      whether or not the craft actually succeeds, so command text never
      shows up as a chat line. All six block `on_break` handlers already
      routed through `vb.world.spawn_item_drop` (this session's earlier
      dropped-item-entity work) rather than `give()` directly, so a crafted
      item is acquired the exact same way a mined one is.
      **Tests:** a new focused unit test
      (`tests/unit/pack_runtime_test.cpp`, "player:take() removes items
      across slots, all-or-nothing") exercises the engine primitive alone,
      via `PackRuntime::dispatch_chat()` directly (no `ServerSession`
      needed — `give`/`take`/`get_inventory` never touch the network layer).
      A new full end-to-end test
      (`tests/unit/content_pack_test.cpp`, "content/base crafting: wood ->
      planks -> sticks via /craft chat") loads the *real* `content/base`
      files through a real `ServerSession`/`ClientSession` pair over
      `LoopbackTransport` and drives the whole example: missing-ingredients
      rejection, a successful two-step craft chain with exact before/after
      counts, an unknown-recipe rejection, and confirms a normal (non-
      `/craft`) chat message still broadcasts untouched. The pre-existing
      "content/base loads cleanly" test's registry-size assertion was
      updated (`base_size + 2`, not `base_size`) since planks/sticks are
      genuinely new blocks, not re-declarations of the Phase 2 base set.
      Verified across both build trees (`build-lua`: full `vb_tests`,
      203/203; `build-asan-nonet`, `VB_WITH_LUA=OFF`: confirms the two
      non-Lua-gated engine changes — `PlayerHandle::take` compiling out with
      the rest of the Lua-gated file, and the plain-filesystem
      `pack_loader.cpp` change — build clean, full `ctest` green).
- [x] `--singleplayer` now runs the real content pack (2026-09-16): closes a
      gap noted since Phase 4.3/5.1 — `src/client/main.cpp`'s `Singleplayer`
      previously wrapped `vb::net::IntegratedGame` with no `PackRuntime`
      involved at all, so `--singleplayer` always ran on the hardcoded
      `BlockRegistry::base()` set with zero scripting, chat commands, or
      item drops, even though the same session's `crafting.lua`/item-drop
      work only actually did anything server-side. Rebuilt `Singleplayer` to
      construct its own `LoopbackNetwork`/`ServerSession`/`ClientSession`
      directly (the same pieces `IntegratedGame` wraps) instead of going
      through `IntegratedGame`, because a `PackRuntime` needs the raw
      server-side `Transport&` (which `IntegratedGame` doesn't expose) and
      needs `install_join_veto()` to run *before* `ServerSession` is
      constructed (order `IntegratedGame`'s single all-in-one constructor
      can't accommodate). Loads `content/base` (hardcoded default, matching
      `server.toml.example`'s own default — no client-side config for this
      exists), wires `attach_world`/`attach_session`/`install_join_veto`
      exactly like `src/server/main.cpp` does, and also wires
      `HandshakeServerHost::block_registry` (previously singleplayer-only
      gap: without it a joining client stays on its own `base()` registry
      and pack-added blocks like `planks`/`sticks` resolve to nothing
      client-side, showing "?" in the hotbar even though give/take work
      fine regardless). A new `Singleplayer::tick()` centralizes
      server/client ticking plus `dispatch_player_join_completed`/
      `dispatch_player_leave`/`dispatch_tick`, mirroring the dedicated
      server's own tick loop, so every one of `main.cpp`'s several
      `sp->game.tick(dt)` call sites collapses to one `sp->tick(dt)`.
      Degrades gracefully, not fatally, if `content/base` can't be found
      (e.g. run from an unexpected working directory) — logs a warning and
      falls back to the hardcoded base block set, deliberately different
      from the dedicated server's "broken pack is fatal" posture, since a
      first-run `--singleplayer` should still just work.
      **Verified with a real headless run, not just a build:**
      `voxel_browser --headless --frames 5 --singleplayer --name CiBot`
      now prints `[base] content pack loaded (boot #1)` and `received block
      registry (10 blocks)` — confirms the pack actually loads, registers
      the two new crafting blocks, and the client receives them, where
      before this fix neither line appeared at all. Full `ctest` green on
      `build-asan-nonet` (the `VB_BUILD_CLIENT=ON` tree, `VB_WITH_LUA=OFF` —
      confirms the refactor doesn't regress the no-Lua stub path either) and
      a clean `voxel_browser`/`vb_tests` build + full `vb_tests` pass
      (203/203) on `build-lua` (`VB_WITH_LUA=ON`, reconfigured with
      `VB_BUILD_CLIENT=ON` to exercise this). **Aside, discovered but out of
      scope:** `server_smoke` fails in a from-scratch `build-lua` ctest run
      specifically because that tree also has `VB_WITH_COMPRESSION=ON` and
      ctest's working directory has no `content/base` (relative-path
      manifest build hits a real I/O error there, unlike the graceful
      degrade above) — pre-existing, unrelated to this fix (`src/server/
      main.cpp` untouched), and not something CI exercises today (no
      workflow turns `VB_WITH_COMPRESSION` on), so left alone; see `STATE.md`.
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
      — `WorldReplicator::apply_block_edit`. **Lua veto: done** — `BlockEditHooks`
      (`inc/vb/net/world_replicator.hpp`) + `PackRuntime::attach_world` wire
      `vb.on("block_break"|"block_place", handler)` as a real pre-apply veto
      (`on_block_edit_before`, `src/script/pack_runtime.cpp`), wired into
      the dedicated server (`src/server/main.cpp`); tested end-to-end over
      `LoopbackTransport` in `pack_runtime_integration_test.cpp`. **No
      dedicated region-protection API** (`ARCHITECTURE_SPEC.md §14`'s
      "per-region protection API" mention) — a pack veto handler already
      receives the player + block position, so a claims/region check is
      just a Lua-side lookup against `vb.storage` inside the same veto;
      nothing further needed C++-side unless a pack wants one built in.
      Tool/hardness times: not yet.
- [x] Apply + bump `revision` + dirty light/mesh + whole-chunk `relight_chunk`.
      `on_break`/`on_place` callbacks: wired (`PackRuntime::Impl::
      on_block_edit_after`). Drops: waits on items (5.1) — the callback's
      return value (a would-be drop) is still logged, not materialized.
- [x] `S2C_BlockEditResult` to the editor + `S2C_ChunkDelta` (block + diffed
      light) fan-out to every player mirroring the chunk.
- [~] Relight on edit: per-chunk from scratch each edit; cross-chunk propagation
      (breaking a floor lets light into the chunk below) still TODO.
- [x] Selection raycast (Amanatides–Woo) + wire-cube highlight; LMB break /
      RMB place stone. Break progress (hold-to-break, 2026-09-16):
      `src/client/main.cpp` now requires LMB held on the *same* voxel for a
      flat `kBreakSeconds` (0.35s) before `C2S_BlockEdit` is actually sent —
      `IsMouseButtonDown` instead of the old instant `IsMouseButtonPressed`;
      switching targets or releasing the button resets progress to zero. A
      small screen-space progress bar (below the would-be crosshair -- none
      exists yet, out of scope here) fills while breaking. Placing is
      unaffected (still an instant `IsMouseButtonPressed`). Purely
      client-side timing gate: the server-side validation/veto path (already
      done, above) is unaffected, since the wire message is identical, just
      sent later. **No per-block hardness/tool system** — one flat duration
      for every block; `REMAINING_TASKS.md`'s "Tool/hardness times: not yet"
      note above is the separately-tracked follow-up for varying it by
      block/tool. Not covered by an automated test (no GL context available
      in this environment, same limitation as the rest of the HUD); verified
      by a clean `/W4` build of `voxel_browser` and the full `ctest` suite
      staying green.

### 5.2 status (2026-09-16): playable over loopback, Lua veto now wired,
hold-to-break landed. `voxel_browser --singleplayer` can break (after a short
hold) and place blocks; a second client sees the change via `S2C_ChunkDelta`;
out-of-reach edits roll back; a pack's
`vb.on("block_break"/"block_place")` can veto a real edit end-to-end (was
already implemented before this session, this pass corrected the checklist
to match — see `STATE.md`'s note the prior status text was stale); breaking
now takes a short hold instead of an instant click. Remaining: drops/tools/
items (5.1), per-block hardness/tool break-time variation, cross-chunk
relight.

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

### 5.4 Play polish  ✅ done (2026-09-16)

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
- [x] Death/respawn (fall out of world, `Health` at 0) with spawn point:
      no new wire message — reuses `S2C_Chat` (no protocol version bump).
      `ServerSession::Conn` gained `spawn_pos` (captured once at join, from
      the same `JoinGrant` that already seeded `move.position`) and `health`
      (`float`, default 20 — matches `ecs::Health`'s default, though the ECS
      component itself remains unused per Phase 3.1's "session drives
      movement directly" deferral; this is plain `ServerSession` state, not
      an EnTT component). New `ServerSession::check_respawns()`, called once
      per tick before `broadcast_snapshots()`: if a player's feet are below
      a configurable `void_kill_y` (`set_void_kill_y`, `ServerConfig::
      void_kill_y` in `server.toml`, default -64.0), `health` is forced to
      0 (instant kill, no partial fall damage this pass); whenever `health
      <= 0` for *any* reason, the player is teleported back to `spawn_pos`,
      `health` reset to 20, their `physics::MoveState` reset (velocity
      zeroed, not just position), `interest_` updated so other players see
      the teleport immediately (not just next input tick), and a private
      `S2C_Chat{"* you died and respawned"}` sent only to that connection
      (not broadcast — reuses the existing chat pipeline/HUD log, same
      "system message via chat" pattern 5.4's join/leave notices already
      established). The `health <= 0` check (not just a direct
      `position.y < void_kill_y` branch) is deliberately generic: nothing
      else decrements health yet (no combat system exists), but any future
      damage source gets working respawn for free by just setting `health`
      to 0. Client-side position correction needs **no new code**: the
      existing prediction/reconciliation pipeline (Phase 3.4) already snaps
      a client to whatever `S2C_EntitySnapshot.local` says next tick, and a
      respawn is just an unusually large snap.
      Tests: a new `tests/unit/netcode_test.cpp` case flies a client
      downward past a `void_kill_y` set 5m below its actual spawn height
      (not a hardcoded absolute Y, so it doesn't depend on what worldgen
      picked for the test seed), asserts the server's authoritative position
      snapped back to spawn height, and asserts the client's chat log
      received `"* you died and respawned"`. `tests/unit/config_test.cpp`
      gained a `void_kill_y` case in its existing TOML-values test. Full
      `ctest` green (4/4) on `build-asan-nonet`; clean
      `-DVB_WARNINGS_AS_ERRORS=ON` build of all three targets.
      Not attempted: no fall damage for a *survivable* fall (only the void
      threshold kills — no minimum-safe-fall-height/damage curve); no death
      message broadcast to *other* players (only the dying player sees the
      notice, matching how the feature is scoped as "with spawn point", not
      a killfeed); `--singleplayer`'s `IntegratedGame` doesn't call
      `set_void_kill_y` explicitly so it just gets `ServerSession`'s
      built-in -64.0 default, untested that this is a sensible number for
      every worldgen seed's actual terrain floor.
- [x] Basic sfx hooks are stubbed (no audio subsystem in v0) — documented in
      `docs/lua-api.md`'s new "Audio / sfx — not implemented" section: no
      `vb.`/`ui.` sound API exists, raylib is built with
      `SUPPORT_MODULE_RAUDIO OFF` (`cmake/Dependencies.cmake`), and
      `ARCHITECTURE_SPEC.md` §10.5's "sfx trigger" mention in the
      block-break event-flow diagram was always illustrative, not a real
      hook. Cross-referenced to this doc's own "Deferred" §'s "Audio
      subsystem + Lua sfx/music API" line, which was already tracking this
      — no code changed, documentation only.

**5.4 status (2026-09-16): all four items landed.** Chat, player list/
join-leave, day/night, and death/respawn — one focused commit each, in that
order, over a single session. None needed cross-cutting changes to the
others; chat's send-to-one-connection pattern got reused as-is by
death/respawn's private notice, and player list's "reuse the chat log for
system messages" precedent is exactly what death/respawn's notice does too.
Everything is verified by unit/integration tests over `LoopbackTransport`
plus a clean full build; none of the four had a live two-window manual
playtest this session (see each entry's own "not re-verified" note) — worth
one before calling Phase 5 itself done.

### 5.5 Documentation  ✅ (2026-09-16)

- [x] `docs/lua-api.md`: refreshed to match reality (2026-09-16) — dropped
      the stale "no base pack yet" header, fixed several outdated claims
      (chat/`send_message`/`open_ui` *are* handled client-side now; `chat`
      fires from a real `C2S_Chat`), documented `vb.world.spawn_item_drop`
      and `player:take()`, and added a "Worked example — content/base"
      table mapping every file in the pack to the API it demonstrates.
      Example-driven per §10.3–10.4 via that table rather than inline
      code samples for every call — `content/base` itself is the running
      example.
- [x] `docs/protocol.md`: already kept in lockstep with every version bump
      throughout Phases 1–5 (every entry in this backlog that touched a
      wire message updated it in the same commit); confirmed current
      (`kEngineProtocolVersion` 11) while working on this item, no drift
      found.
- [x] README "Getting Started" (2026-09-16): replaced the stale, purely
      forward-looking "Implementation Strategy" phase plan with an
      accurate "Project Status" table (Phase 0–5 done/substantially done,
      linking to this file for the real backlog); corrected the Tech Stack
      table to distinguish what's actually driving the game today from
      still-gated future backends (Cellulose/librg/EnTT/FastNoise2, each
      behind its own `VB_WITH_*` flag); added a real "try it now"
      `--singleplayer` quick-start (which prompted actually fixing
      `--singleplayer` to run the real content pack — see 4.3/5.1's
      "`--singleplayer` now runs the real content pack" entry above).
      Build steps + `VB_WITH_NET`'s protobuf system deps were already
      present from Phase 0/1.2 and needed no further work.
- [x] `content/base` as tutorial pack: every file already carries
      "why, not just what" comments (established well before this pass —
      see e.g. `blocks/dirt.lua`'s explanation of `add_or_get` idempotency,
      or `crafting.lua`'s explanation of why crafting logic lives in
      content, not the engine); confirmed still true while writing the
      `docs/lua-api.md` worked-example table above, no gaps found.
- [x] `CONTRIBUTING.md` (new, 2026-09-16): module map (every `src/`/`inc/vb/`
      subdirectory → target → purpose), build/test workflow tips beyond
      what's in the README (multiple build trees for `VB_WITH_LUA` on/off,
      doctest's glob-not-substring `--test-case` filter, which smoke tests
      need which `VB_BUILD_*` flags), code style (`.clang-format`, no
      exceptions on engine hot paths, `vb::<module>` namespacing), a
      step-by-step for adding a new wire message (struct → `MessageType` →
      round-trip test → version bump → `docs/protocol.md` entry →
      integration test), and a "generic primitive, not a game-specific
      one" guideline for new Lua bindings using this session's crafting
      work as the worked example.

**Phase 5 exit:** build from source on all 3 platforms; run a server with the
base pack; two players connect, mine and place blocks, see each other, chat, and
open the inventory UI. Nothing gameplay-facing is hardcoded in C++.
**Status:** met over `LoopbackTransport`/single-process verification
throughout this backlog (see each phase's own status paragraph); the literal
"two players, two real windows" manual playtest across all 3 platforms has
not been run by a human yet — every session so far has verified through
automated tests + `--headless` runs (no GL context in this environment). Real
multiplayer over `GnsTransport` itself *is* covered by a real-UDP unit test
(`gns_transport_test.cpp`) and real client/server processes were smoke-tested
manually earlier in the project (see `STATE.md`'s Phase 1 notes) — what
hasn't specifically been re-verified live is the *combination*: two real
windows, chatting, crafting, and seeing each other, all at once.

---

## Phase 6 — Lua-Driven Extensibility (design only, not started)

> Design agreed in discussion on 2026-09-17: four systems that let content
> packs override/extend engine defaults (biomes, entities, UI, input, data)
> the way the register-by-name registries already let blocks be overridden
> today. Builds on 3.1's EnTT wiring, 4.2's registration API, and 4.2's
> still-open "wire `register_entity` callbacks" item — see
> `ARCHITECTURE_SPEC.md` §7.1-7.2, §10.3-10.6, §17, §19 Q6 for the design.

### 6.1 Entity kinds as classes, spawned entities as objects

- [ ] Per-instance Lua state: the `ScriptState` component (`ARCHITECTURE_SPEC.md`
      §7.1, already in the base component table but unused) holds a table per
      spawned entity, passed as `self` to every callback, so two objects of
      the same `register_entity` kind track independent data.
- [ ] Wire `on_spawn`/`on_tick`/`on_hit`/`on_death` into `ScriptPreTickSystem`/
      `ScriptPostTickSystem` — same item already tracked under 4.2 ("waits on
      3.1's EnTT registry"); the `self`-table plumbing above has to land
      alongside it, not after, since it changes the callback signature.
- [ ] Decide: do accessor methods on `self` cover only base components
      (`get_pos`, mirroring the player object), or can `self` expose
      arbitrary kind-specific fields directly? Leaning arbitrary fields for
      custom data, accessors for engine-owned components.

### 6.2 Fully Lua-defined, immediate-mode reactive UI

- [ ] Reframe `ui.define` from "declare a static screen" to "register a
      `render(state)` function called every UI frame" (§10.4) — `raygui` is
      already immediate-mode, so no virtual-DOM diffing is needed: the C++
      side just walks whatever `render_fn` returns that frame and issues the
      matching `raygui` calls. State mutation naturally reflows next frame.
- [ ] Real API break from `content/base/ui/{inventory,pause}.lua`'s current
      static-declaration style — those need rewriting to the new shape, not
      just extending, once this lands.
- [ ] Event handlers keep the existing `C2S_UiEvent` round-trip
      (server-authoritative for anything that matters); purely cosmetic
      state (hover, scroll) can stay client-local.

### 6.3 Server-side player-input interception, closed-schema custom keybinds

- [ ] `vb.register_keybind(name)` at pack load — idempotent registry, frozen
      at `PackRuntime::freeze()`, same pattern as blocks/entities.
- [ ] Sync the registered set to the client at handshake (same shape as
      `S2C_BlockRegistry`, §4.3); the wire only ever encodes a bounded bitset
      indexed by registration order — an unregistered key cannot be
      represented on the wire at all. This closed schema is the flood
      defense, not a post-receipt filter (§17).
- [ ] Movement input (`PlayerInput`'s existing fields) is untouched — this
      channel is additive, for pack-defined shortcuts only.
- [ ] New `vb.on("player_input", handler)` fires in `IngestInputSystem`,
      before `MovementIntegrationSystem` runs (§7.2). Handler may veto
      (`return false`) or return a replacement input table before
      integration — covers both "block movement" and "reinterpret it" (e.g.
      a dash ability).
- [ ] Per-connection rate limiting on custom-keybind events, defense in depth
      on top of the closed schema — folds into the already-tracked
      "per-player rate limit / flood guard belongs with `GnsTransport`" item
      (Phase 1.3).

### 6.4 Generic per-key persistent storage (script-owned identity/auth)

- [ ] `vb.db.get(key)` / `vb.db.set(key, value)` / `vb.db.delete(key)` —
      arbitrary script-chosen keys (`"user:" .. name`, `"session:" .. token`,
      ...), distinct from the existing pack-global `vb.storage`. The engine
      has no concept of "logged in" — a connection stays just a connection
      (as today) until a pack's own login flow looks up a record and decides
      to recognize it. Joining a world isn't authenticating, the same way
      loading a webpage isn't.
- [ ] Storage backend: today's single `storage.json` blob doesn't scale to
      one record per identity — needs an actual per-key store (SQLite is the
      leading candidate, common well-trodden dependency) once this lands.
      Implementation detail, not a design blocker.
- [ ] Expose a minimal `vb.crypto.hash(...)` primitive so packs implementing
      their own login don't roll credential hashing in pure Lua — the
      sandbox strips `os`/`io` deliberately (§10.2), and pure-Lua hashing is
      slow and easy to get wrong. The engine still takes no position on auth
      as a concept — see `ARCHITECTURE_SPEC.md` §19 Q6.

### 6.5 Shared block-damage breaking (default + override crack texture)

- [ ] `BlockType` gains `max_damage` (0 = today's instant break, the
      default — no behavior change for any existing block) and an optional
      `crack_texture` override (§5.2). `vb.register_block{...}` exposes both.
- [ ] Sparse server-side damage map (`pos → {damage, max_damage,
      last_touched_tick}`), only holding entries with damage > 0 — does not
      touch chunk revisions or mesh invalidation.
- [ ] Ride the *existing* interest/replication system (§8.4) for visibility
      rather than a new channel: a damaged block is a transient
      interest-managed record, spawned when damage > 0, despawned at 0 —
      reuses the spawn/despawn diffing every other replicated object already
      gets, so everyone nearby sees cracks form and vanish for free.
- [ ] `C2S_BlockBreakBegin{pos, face}` / `C2S_BlockBreakStop{pos}` bracket a
      player holding on a target; same reach/tool/protection checks as
      `C2S_BlockEdit` today gate entry via `vb.on("block_break_begin", ...)`
      (vetoable).
- [ ] `vb.on("block_break_tick", handler)` fires once per tick **per
      contributing player** while held — returns the damage delta to add.
      Multiple players contributing to the same block sum concurrently
      ("breaking together"). Engine has no opinion on tool speed,
      enchantments, or anything the delta is computed from.
- [ ] `vb.on("block_health_tick", handler)` fires once per tick **per
      damaged block**, regardless of contributors — `(pos, damage,
      max_damage, ticks_since_last_hit)` in, new damage value (or unchanged)
      out. No heal / full heal / gradual decay / heal-after-idle are all
      just what the handler computes; no handler registered = permanent
      damage, no built-in default policy.
- [ ] Completion (summed damage reaches `max_damage`) drives the *existing*,
      unchanged `C2S_BlockEdit`/`BlockEditSystem`/`on_break` pipeline — this
      system only gates when that fires, doesn't replace it.
- [ ] Default generic crack overlay (progressive stages by damage ratio)
      ships so breaking looks right with zero scripting; `crack_texture`
      override follows the same override-by-name convention as every other
      registry. **Blocked on** the still-pending real texture/atlas system
      (4.3/5.1 — client is untextured cubes today) landing first.

### 6.6 Player damage & death (foundational — split out from the rest below)

> Audited 2026-09-17: `void_kill_y` (`inc/vb/core/config.hpp:31`) is
> currently **the only damage source in the entire engine** — no fall
> damage, no PvP, no mob damage, no hunger/starvation. `Health` exists on
> the entity but nothing but falling into the void can ever reduce it. This
> is upstream of combat, PvP, and hazards, so it's called out on its own
> rather than folded into the rest of 6.7-6.13 — most of that later content
> depends on this landing first.

- [ ] A generic damage primitive (e.g. `player:damage(amount, cause)`) —
      doesn't exist today; currently the only way to reduce `Health` is the
      hardcoded void-kill check. Without this, no pack can implement combat,
      PvP, fall damage, hunger, or mob attacks no matter what else lands.
- [ ] `check_respawns()` (`src/net/session.cpp:442-470`) is entirely
      hardcoded: instant full heal, teleport to `state.spawn_pos`
      unconditionally, a fixed chat string, no drops-on-death, no
      death-cause info. **Raw state, Lua decides:** fire a hook with
      `(player, cause, health_before)` and let Lua decide heal amount,
      respawn point, whether to drop inventory, and the message text.
- [ ] Spawn point is a single fixed world column (`worldgen::
      default_spawn_position`, always `(0,0)`), set once at join and reused
      for every respawn forever (`session.cpp:307,456,464`) — no
      bed/checkpoint/team-spawn concept can exist today. **Raw state, Lua
      decides:** a per-respawn "pick spawn point" hook, engine just calls it
      when a respawn is about to happen.
- [ ] No PvP toggle exists, and none would be meaningful yet since there's
      no damage system for it to gate — not a separate item, just confirms
      it's downstream of the primitive above.

### 6.7 Physics / movement parameters (default + override)

- [ ] `physics::MoveParams` (`inc/vb/physics/movement.hpp:18-33` — gravity,
      jump speed, walk/sprint speed, accel, friction, step height, fly
      speed) is a hardcoded struct whose own comment already says *"Engine
      defaults; a Lua pack overrides per entity kind"* — planned, never
      wired up. Enables double-jump, low-gravity zones, custom movement
      abilities per entity kind.
- [ ] `ServerConfig.gravity` (`inc/vb/core/config.hpp`) duplicates
      `MoveParams.gravity` as a separate server-operator setting — worth
      reconciling which one wins once physics is Lua-overridable, so an
      operator's `server.toml` and a pack's override don't silently fight.

### 6.8 Day/night cycle curve (default + override)

- [ ] `sky_brightness()`/`sky_color_for_time()` (`inc/vb/world/
      daynight.hpp:26-39`) are a fixed 4-keyframe gradient, not Lua-reachable
      at all today. `day_length_seconds` already exists as a runtime value
      (`ServerSession::set_day_length_seconds`) but isn't wired to any
      config or Lua surface either. Ship the current curve as the default;
      let a pack supply its own keyframes/curve function (eternal night,
      custom skyboxes, alien day cycles).

### 6.9 Inventory stacking (default + override)

- [ ] No max stack size or slot cap exists anywhere; `PackRuntime::give()`
      (`src/script/pack_runtime.cpp:346-352`) always pushes a new slot,
      never combines. More a missing feature than a hardcode, but same
      shape: ship a default stack cap (e.g. 64) and slot count, let
      `register_item` override its own stack size (non-stackable tools vs.
      stackable blocks), and let Lua opt into combining logic.

### 6.10 Chat transform/moderation hook

- [ ] `handle_chat()` (`src/net/session.cpp:185-204`) hardcodes the message
      format (`name: text`), has no rate limit, and the existing
      `vb.on("chat")` hook is veto-only (`return false` or nothing) — it
      can't transform the text. Extend it to the same veto-or-replace shape
      already designed for `player_input` (`ARCHITECTURE_SPEC.md` §10.6), so
      a pack can do profanity filtering, custom formatting, or its own
      rate-limit policy instead of the engine's none-at-all.

### 6.11 Item drop parameters (default + override)

- [ ] `pickup_radius` (default 1.5) and `lifetime_seconds` (default 120.0)
      on `world::ItemDropTickResult` (`inc/vb/world/item_drops.hpp:63-67`)
      are fixed at C++ construction, uniform across every item type, no Lua
      reach at all. Expose as engine defaults, let `register_item` override
      per-item (a magnet-radius power-up, a rare drop that never despawns).

### 6.12 Entity animation clip priority (cosmetic, low priority)

- [ ] `resolve_anim_clip()`'s fixed priority order (`kDead > kHurt > kActing
      > kJump/kFall > kRun > kWalk > kIdle`) and its speed thresholds
      (`AnimThresholds`, `inc/vb/render/entity_visual.hpp:44-48`) stay
      engine-fixed even once per-kind clip *assets* are pack-defined
      (already tracked separately under 4.2's `visual = {...}` sub-table) —
      *which* clip wins in a given state is a distinct, finer-grained
      concern. Cosmetic only; lowest priority in this section.

### 6.13 Read-only server config visibility (not a pack-override surface)

- [ ] `ServerConfig` (`tick_rate`, `view_distance`, `max_players`,
      `void_kill_y`, ...) are server-**operator** settings
      (`server.toml`/CLI), a different persona from a content-pack author —
      a pack should not be able to silently change `max_players` out from
      under the operator running the server. At most, expose a read-only
      `vb.config.get(key)` so a pack can *react* to these values (e.g. tune
      spawn density to view distance), not a full override registry like
      6.6-6.11 above.

> World generation (biome selection, height params, block choice — all
> still 100% hardcoded in `WorldGenerator::generate` today) is not repeated
> here; it's already fully tracked under Phase 4.2 / the worldgen items in
> Phase 6's intro note.

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
- Rule-based decorative structure placement (trees, ruins, rock formations)
  for the worldgen decoration pass (`ARCHITECTURE_SPEC.md` §6 stage 6):
  structures authored in a dedicated external tool and imported into the
  content pack as a schematic, placed by declarative rules (neighbor-block
  constraints — e.g. "must be on dirt", clustering tendency, biome/density
  weighting) rather than every structure needing a hand-written procedural
  callback. Explicitly post-first-playable — depends on the Lua-driven
  worldgen pipeline itself (Phase 4.2/6) landing and settling first; noted
  now so the decoration-pass design leaves room for it.
