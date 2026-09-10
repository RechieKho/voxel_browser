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
- [ ] `GnsTransport` over `ISteamNetworkingSockets`: init/shutdown, real UDP,
      per-connection lifecycle. **(needs `VB_WITH_NET`; Phase 1.2 spike)**
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
- [ ] Per-IP connection cap belongs to the server loop once `GnsTransport` lands.
- [ ] `ENGINE_PROTOCOL_VERSION` mismatch → both FSMs already reject; surface it
      in the client connect UI (Phase 5.3 main menu).

### 1.4 Replication bootstrap (`vb_core/replication`)

- [ ] **(spike)** librg v0.x API: streamer setup, interest radius, entity
      track/untrack, event callbacks, serialization use. Write findings to
      `docs/protocol.md`.
- [ ] `librg` world init on server; per-player interest sphere.
- [ ] Minimal `S2C_EntitySnapshot` carrying `{net_id, pos}` for player entities.
- [ ] Integration test: two headless clients connect; each receives the other's
      snapshot when within interest range, loses it outside. *(README Phase 1
      acceptance)*

### 1.5 Client shell (`vb_render`)

- [x] Window/context via raylib, main loop scaffold, `--headless` no-op renderer
      (`vb/render/window.hpp`).
- [x] `FirstPersonController` (`vb/render/camera.hpp`, header-only, unit-tested):
      mouse-look with pitch clamp, WASD + vertical + sprint, normalized diagonals,
      double precision. Client fills `LookMoveInput` from raylib; mouse-capture
      toggle on click / Tab / Esc.
- [x] Debug overlay: position, yaw/pitch, FPS, connection status line.
- [x] Placeholder grid/cube render until Phase 2 meshing.
- [ ] Spawn/orient from `S2C_JoinAccept` in the *multiplayer* path too (only the
      singleplayer path feeds spawn_pos in so far).

**Phase 1 exit:** `voxel_browser_server` accepts connections; `voxel_browser`
connects, completes handshake, opens a window; integration test for mutual
visibility of two clients is green in CI.

**Phase 1 status (2026-09-10):** core primitives, TOML config, wire codec,
handshake FSMs, transport abstraction + loopback backend, session layer,
integrated singleplayer, and the client shell are done and tested. Remaining
before the exit criterion: `GnsTransport` for real sockets (1.2) and the librg
replication spike + two-client visibility test (1.4) — both need `VB_WITH_*`
deps turned on and, for GNS, new CI system packages.

---

## Phase 2 — World State & Terrain Generation

Goal: server generates terrain, streams chunks, client meshes and renders them.

### 2.1 Voxel data model (`vb_core/world`)

- [ ] `PalettedChunkStore`: palette + bit-packed indices (1/2/4/8/16 bpv),
      homogeneous collapse, get/set, iteration. Unit-tested round-trips.
- [ ] `Chunk`: block store + light volume + dirty flags + gen state + `revision`.
- [ ] `World`: `ChunkCoord → Chunk` map, load/unload manager.
- [ ] `BlockSolidQuery` interface (shared by physics + meshing).
- [ ] `CHUNK_DIM = 32` constant + coordinate math (world↔chunk↔local).

### 2.2 World generation (`vb_core/worldgen`)

- [ ] FastNoise2 wrapper: build node trees from a parameter struct.
- [ ] Hardcoded default pipeline first (heightmap → dirt/stone/air), Lua-driven
      pipeline deferred to Phase 4.
- [ ] Worldgen worker pool + lock-free result handoff to the tick thread.
- [ ] Surface pass (grass/dirt/sand by height vs. water level).
- [ ] Determinism test: `(seed, coord)` → stable chunk hash, cross-platform
      golden value in CI.

### 2.3 Lighting

- [ ] Sky + block light flood fill on chunk generation.
- [ ] Incremental relight on block edit (Phase 3/5 consumer).

### 2.4 World replication (§8.5)

- [ ] Messages: `S2C_ChunkAdd`, `S2C_ChunkDelta`, `S2C_ChunkRemove`.
- [ ] Palette container serialization + LZ4 compression.
- [ ] `InterestManagementSystem`: per-player visible chunk set from position +
      view distance; diff → add/remove messages.
- [ ] `ChunkLifecycleSystem`: generate/load chunks around players, unload when
      no player is interested.
- [ ] Client `ClientChunkStore` mirror; apply add/delta/remove.

### 2.5 Client meshing (`vb_render/render`)

- [ ] **(spike)** Cellulose API: meshing entry points, does it own GPU buffers or
      emit vertex data, neighbor/light inputs, chunk granularity. Record in spec
      open question #2.
- [ ] Mesh worker pool: greedy mesh from `(registry, blocks, light, neighbors)`.
- [ ] Per-vertex light + AO baking.
- [ ] Main-thread GPU upload with per-frame budget.
- [ ] Frustum + distance culling; transparent second pass.
- [ ] Texture atlas builder from pack textures (hardcoded set until Phase 4).

**Phase 2 exit:** connect to a server and fly around streamed, meshed terrain
(dirt/stone/grass/air) with correct chunk load/unload; determinism CI gate green.

---

## Phase 3 — Entity Component System & Physics

Goal: server-simulated players with voxel collision, movement synced to clients
with prediction/interpolation.

### 3.1 ECS (`vb_core/ecs`)

- [ ] EnTT registry wiring on the server; fixed 20 Hz tick loop with accumulator.
- [ ] Base components (§7.1): `Position`, `Velocity`, `Rotation`, `AABB`,
      `Collider`, `PlayerInput`, `PlayerTag`, `NetReplicated`, `EntityKind`,
      `Health`, `Inventory`, `ItemStack`, `ScriptState` (empty until Phase 4).
- [ ] System runner with explicit ordering (§7.2).
- [ ] Client-side lightweight registry (`NetId`, interp `Position` prev/current,
      `Rotation`, `EntityKind`, `RenderHandle`).

### 3.2 Input pipeline

- [ ] `InputCmd { seq, dt, move, look, buttons }`; client samples per frame,
      keeps a history ring, batches on lane 4 (`C2S_InputBatch`).
- [ ] `IngestInputSystem`: per-player queue, validation, rate limit, dt clamp.

### 3.3 Physics (`vb_core` shared)

- [ ] Swept-AABB vs. voxel collision, per-axis resolution, `on_ground`, step-up.
- [ ] Gravity integration from config.
- [ ] Single implementation consumed by `VoxelCollisionSystem` (server) and
      client prediction, parameterized by `BlockSolidQuery`.
- [ ] Unit tests: floor rest, wall slide, ceiling bonk, step-up, corner cases.

### 3.4 Replication + netcode (§8.4)

- [ ] Map EnTT player entities ↔ librg network entities.
- [ ] `S2C_EntitySnapshot` full form: `server_tick`, `last_acked_input_seq`,
      per-entity `{net_id, kind?, pos, rot, vel, flags}`, spawn/despawn records
      resent until acked.
- [ ] Client interpolation of remote entities at `server_time_est - 100 ms`.
- [ ] Local-player prediction + reconciliation: snap to authoritative state,
      replay unacked inputs through shared movement code.
- [ ] Server-time estimation / smoothing on the client.
- [ ] Integration test: client input → predicted move matches server result
      within epsilon after reconciliation; second client sees interpolated motion.

**Phase 3 exit:** two players walk around shared terrain, colliding with voxels,
smooth on each other's screens, local motion is responsive (predicted).

---

## Phase 4 — The "Browser" Engine (Scripting & Assets)

Goal: server logic + content defined in Lua; client auto-downloads pack assets;
Lua-defined UI.

### 4.1 Lua runtime (`vb_core/script`)

- [ ] Embed Lua 5.4; `vb::script::Vm` wrapper (open, load chunk, call, error
      capture with traceback).
- [ ] Decide raw API vs. sol2 (open question #1) and commit.
- [ ] Sandbox env (§10.2): strip `os`/`io`/`debug`/bytecode-`load`; custom
      `require` over the virtual pack FS; instruction-count hook; memory ceiling
      allocator; per-callback wall-clock budget enforced by the tick loop.
- [ ] Sandbox-escape test suite (attempts to reach fs/os/net all fail).

### 4.2 Server Lua API (§10.3)

- [ ] Registration API: `register_block`, `register_item`, `register_entity`,
      `register_biome`, `register_craft`, `worldgen.set_pipeline`.
- [ ] Freeze registries after pack load; assign stable `BlockId`s.
- [ ] Runtime world API: `get_block`/`set_block`/`raycast`/`spawn`.
- [ ] Entity + player API: pos/velocity/inventory/messages/`open_ui`/`give`.
- [ ] Event bus: `vb.on("player_join"|"player_leave"|"block_break"|
      "block_place"|"player_interact"|"chat"|"tick", handler)` with veto returns.
- [ ] Scheduling: `vb.after`, `vb.every`.
- [ ] `vb.storage` JSON-backed persisted KV.
- [ ] `EntityKind` tick/spawn/hit/death callbacks wired into `ScriptPreTick` /
      `ScriptPostTick` systems.
- [ ] Lua-driven worldgen pipeline replaces the Phase 2 hardcoded one; biomes.

### 4.3 Block registry replication

- [ ] `S2C_BlockRegistry` message: full block table (name, flags, light, model,
      face textures as asset refs, collision).
- [ ] Client rebuilds a read-only registry; meshing + atlas now driven by it.

### 4.4 Asset Sync Protocol (§9)

- [ ] Server startup: walk pack dir, normalize + reject unsafe paths, hash
      (xxHash3-128), build `AssetManifest` + `manifest_hash`.
- [ ] Messages: `C2S_AssetManifestRequest`, `S2C_AssetManifest`,
      `C2S_AssetRequest`, `S2C_AssetData` (chunked, lane 3), progress accounting.
- [ ] Client content-addressed cache (`~/.cache/voxel_browser/assets/...`) +
      index + LRU eviction to a configurable cap.
- [ ] Transfer state machine: request misses, stream, per-file hash verify,
      commit; abort connection on mismatch or size-cap breach.
- [ ] Virtual pack filesystem in memory (path → cached bytes) feeding Lua
      `require`, texture loader, model loader, UI loader.
- [ ] Handshake ordering: manifest + assets before `S2C_BlockRegistry` /
      `JoinAccept` (§8.3).
- [ ] Reconnect fast-path: matching `manifest_hash` skips the exchange.
- [ ] Integration test: base pack (scripts + textures) syncs to a cold client,
      second connect transfers nothing.

### 4.5 Client UI VM + raygui (§10.4)

- [ ] Separate restricted client VM (no world/net/fs; `ui` table + read-only
      client state only).
- [ ] `ui.define(name, layout_fn)` declarative layout (panel, label, button,
      list, item grid, text input).
- [ ] C++ renderer mapping layout → raygui immediate-mode calls.
- [ ] `player:open_ui(name, ctx)` server→client; `on_click/on_change/on_close`
      → `C2S_UiEvent` RPC back to the server VM.
- [ ] Base pack `ui/inventory.lua`, `ui/pause.lua`.

**Phase 4 exit:** server runs the base Lua pack; a cold client connects,
downloads scripts + textures, receives the block registry, and opens a
Lua-defined inventory screen.

---

## Phase 5 — Minimum Playable Base

Goal: a small, coherent, playable multiplayer sandbox.

### 5.1 Base content pack (`content/base`)

- [ ] `pack.toml`, `init.lua`.
- [ ] Blocks: dirt, grass, wood, leaves, stone, sand (+ air) — `blocks/*.lua`,
      16×16 textures, correct solid/opaque/model flags.
- [ ] Biome(s): plains/forest with surface rules + simple tree decoration.
- [ ] Dropped-item entity; basic inventory + hotbar.
- [ ] Simple crafting recipes (wood → planks → sticks, etc.) — optional.

### 5.2 Block breaking / placing over the network (§8.5)

- [ ] `C2S_BlockEdit` (break/place, `predicted_seq`, pos, face, block_id?).
- [ ] Client optimistic apply + rollback on reject.
- [ ] Server validation: reach distance, target validity, tool, Lua
      `block_break`/`block_place` veto, region protection API.
- [ ] `BlockEditSystem`: apply, bump `revision`, dirty light + mesh, fire
      `on_break`/`on_place`, emit drops.
- [ ] `S2C_BlockEditResult` + `S2C_ChunkDelta` fan-out to interested players.
- [ ] Incremental relight on edit verified visually + in tests.
- [ ] Selection raycast + block highlight + break progress on the client.

### 5.3 Main menu (raygui, engine-level, not pack)

- [ ] Server address + port input, player name, Connect button.
- [ ] Connection progress screen (handshake state + asset download bar).
- [ ] Error/disconnect screen with server-provided reason.
- [ ] Recent servers list (local `client.toml`).
- [ ] Settings screen: resolution, vsync, FOV, render distance, sensitivity,
      keybindings, cache size.
- [ ] Integrated-server path for singleplayer (in-process server on localhost).

### 5.4 Play polish

- [ ] Day/night `time_of_day` from `JoinAccept`, advanced server-side, simple sky
      gradient client-side.
- [ ] Chat: `C2S_Chat` / `S2C_Chat`, `vb.on("chat")` veto/route, HUD chat box.
- [ ] Player list / join-leave messages.
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
