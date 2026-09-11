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
- [ ] Spawn/orient from `S2C_JoinAccept` in the *multiplayer* path too (only the
      singleplayer path feeds spawn_pos in so far).

**Phase 1 exit:** `voxel_browser_server` accepts connections; `voxel_browser`
connects, completes handshake, opens a window; integration test for mutual
visibility of two clients is green in CI.

**Phase 1 status (2026-09-10):** everything except the real `GnsTransport`
socket backend (1.2). Core primitives, TOML config, wire codec, handshake FSMs,
transport abstraction + loopback backend, session layer, integrated
singleplayer, client shell, interest management + entity snapshots + the
two-client visibility test — all done and tested (~280 assertions). The librg
integration is deferred to Phase 3 (needs moving players to be meaningful); the
`Transport` seam is where `GnsTransport` plugs in, and the replication test just
needs re-running over it.

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
- [ ] Cross-chunk sky occlusion + incremental relight-on-edit — Phase 3/5;
      reuses the same propagation core.

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
- [ ] Mesh worker pool (needs a chunk+neighbour snapshot), frustum culling,
      transparent second pass, texture atlas (Phase 4) — follow-ups.

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

### 3.4 Replication + netcode (§8.4)

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

**Phase 3 exit:** two players walk around shared terrain, colliding with voxels,
smooth on each other's screens, local motion is responsive (predicted).

**Phase 3 status (2026-09-11): substantially met over the loopback transport.**
Shared voxel physics, the input pipeline, authoritative server movement, client
prediction/reconciliation and remote interpolation are done and tested; the
client (`--singleplayer`) now walks input-driven with terrain collision instead
of the free-fly cam. Deferred: a formal EnTT registry + system runner (Phase 3.1
— not blocking; revisit when Lua entities need it), wall-clock server-time
estimation and the librg entity mapping (both need the real `GnsTransport`).

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
