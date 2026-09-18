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
- [x] Per-IP connection cap belongs to the server loop. Done 2026-09-18:
      `Transport::remote_address(ConnId) -> optional<string>` (new, default
      `nullopt`; `GnsTransport` overrides it via `GetConnectionInfo`;
      `LoopbackTransport` keeps the default -- no real network identity
      in-process) + `ServerSession::set_max_connections_per_ip(int)` (`0` =
      unlimited default), enforced in `tick()`'s `kConnected` handling by
      counting already-tracked `Conn::remote_address` matches and
      `Transport::close()`-ing the new one before any handshake traffic if
      the cap is already met. Wired to a new `ServerConfig::
      max_connections_per_ip`/`server.toml`'s own key (`src/server/
      main.cpp`); no CLI flag, same as `void_kill_y`/`day_length_seconds`/
      `asset_max_*`. Also exposed read-only via `vb.config.get(
      "max_connections_per_ip")` (Phase 6.13's surface). New tests in
      `gns_transport_test.cpp` (`remote_address()` returns the real peer IP
      over UDP; a `ServerSession` with the cap set to 1 accepts a first
      connection and transport-closes a second from the same address before
      it can even join) plus `config_test.cpp`/`pack_runtime_test.cpp`
      coverage for the new field.
- [ ] `ENGINE_PROTOCOL_VERSION` mismatch → both FSMs already reject; surface it
      in the client connect UI (Phase 5.3 main menu).
- [x] Hostname resolution: `connect()` only accepts numeric IP literals today
      (`SteamNetworkingIPAddr::ParseString` doesn't resolve DNS). "localhost" /
      real hostnames need `getaddrinfo` in `GnsTransport::connect`. Done
      2026-09-18: `resolve_hostname()` (`src/net/gns_transport.cpp`) falls
      back to `getaddrinfo`/`freeaddrinfo` (portable BSD-sockets resolver,
      `<winsock2.h>`/`<ws2tcpip.h>` on Windows scoped in its own
      `WSAStartup`/`WSACleanup` pair, `<netdb.h>` on POSIX) whenever
      `ParseString()` rejects the input as non-numeric, preferring the
      first IPv4 result and falling back to IPv6. New test in
      `gns_transport_test.cpp`: `connect("localhost", ...)` completes a
      real handshake over UDP; a genuinely unresolvable hostname
      (`*.invalid`, RFC 2606) still fails `connect()` cleanly, same as
      before.
- [ ] macOS CI doesn't build `VB_WITH_NET` yet: it's a universal (arm64+x86_64)
      build, but a brew-installed protobuf is single-arch, which breaks linking
      the other slice. Needs a universal protobuf (vcpkg triplet, or building
      protobuf from source for both arches) — see `build_macos.yml`.
- [x] Two live `voxel_browser` + `voxel_browser_server` processes have not
      been run against each other manually yet — validated so far by
      `gns_transport_test.cpp` (raw transport, one process) and the existing
      Loopback-based session/handshake/replication tests (application logic,
      generically transport-agnostic). Worth an actual two-terminal smoke test.
      Done 2026-09-18: `voxel_browser_server.exe --port 27099` in one
      background process, `voxel_browser.exe --headless --frames 5 --server
      127.0.0.1 --port 27099` in a second, separate process (real UDP over
      loopback, `VB_WITH_NET`, `build-net-lua`) — real join completes
      (`S2C_BlockRegistry`, `S2C_MoveParams`, `join_accept` all received,
      client exits cleanly after its frame budget). No code changed; this
      was purely a manual verification step, not a code gap.

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
- [x] Biome selection (2.2 step 2) + carvers + vein/scatter + decoration pass
      — deferred to the Lua pipeline (Phase 4); base pipeline is
      heightmap-only for now. Biome selection design updated 2026-09-17 to
      Voronoi-cell partitioning with adjacency-weighted probability
      (WFC-flavored, non-backtracking — see `ARCHITECTURE_SPEC.md` §6 stage
      2), not the originally-sketched continuous temperature/humidity noise.
      Landed 2026-09-18 as Phase 6.14 (`vb.worldgen.set_pipeline` +
      `vb.register_biome` + `vb/worldgen/biome_selector.hpp`) — see that
      item for the full writeup. The fixed base pipeline itself stays
      heightmap-only exactly as this bullet always said; the Lua pipeline is
      the opt-in layer on top, per 6.14's own "no call, no pipeline" posture.

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

### 2.5 Client meshing  ✅ (hand-rolled, permanent — Cellulose evaluated and reverted)

- [x] **(spike)** Cellulose API — resolved in `ARCHITECTURE_SPEC.md §19 Q2`.
      Emits vertex data (`ChunkMesh`); reusable seam is `greedy_mesh(vector<
      MeshSample>, …)`. Wired in behind `VB_WITH_MESHING` (2026-09-16), but
      reverted after its more volatile greedy-merged vertex/index counts
      reproduced the NVIDIA VAO/VBO-churn crash documented in `STATE.md`
      §1/§8 — see §19 Q2's updated resolution note and `STATE.md` §8's 15th
      entry. The `VB_WITH_MESHING` flag and Cellulose `FetchContent` block
      were later removed outright (2026-09-17); not a planned swap-in
      anymore.
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
occlusion + relight-on-edit (Phase 3/5), texture atlas (Phase 4), LZ4 chunk
compression (`VB_WITH_COMPRESSION`). Greedy merge (Cellulose,
`VB_WITH_MESHING`) is no longer deferred-but-planned — it was tried, reverted
(2026-09-16, see 2.5 above), and the dependency removed outright
(2026-09-17); the mesh worker pool itself shipped (2026-09-15) with the
hand-rolled per-face mesher.

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
- [x] Lua-driven worldgen pipeline replaces the Phase 2 hardcoded one —
      moved to Phase 6.14 (extensibility push, FastNoise2 backend); tracked
      there now instead of here. Landed 2026-09-18 — see that item.
- [ ] `register_entity`'s `visual = {...}` sub-table (`variant` frame-size
      lookup, `texture`, `facings`, `origin`, `clips` grid) for 3.5's
      `entity_renderer`, schema finalized 2026-09-17 — see `ARCHITECTURE_SPEC.md`
      §11.3. Not implemented yet; nothing reads a per-kind visual def (3.5
      still hardcodes a flat placeholder). Also needs the per-instance
      `entity.visual_override` merge (`ScriptState`) for reskins (e.g. player
      skins) once this lands.

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
- [x] `ui.define(name, render_fn)` declarative layout: `label`, `panel`,
      `button`, `list`, `text input` (spec's set minus **item grid**,
      deferred — needs real items, waits on 5.1). Originally evaluated once
      at `open()` and never re-laid-out afterward; superseded by Phase 6.2's
      per-frame `render(state)` reactive model below.
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

## Phase 6 — Lua-Driven Extensibility  ✅ 6.1–6.16 done (2026-09-18); 6.17 planned

> Design agreed in discussion on 2026-09-17: four systems that let content
> packs override/extend engine defaults (biomes, entities, UI, input, data)
> the way the register-by-name registries already let blocks be overridden
> today. Builds on 3.1's EnTT wiring, 4.2's registration API, and 4.2's
> still-open "wire `register_entity` callbacks" item — see
> `ARCHITECTURE_SPEC.md` §7.1-7.2, §10.3-10.6, §17, §19 Q6 for the design.

### 6.1 Entity kinds as classes, spawned entities as objects  ✅ (2026-09-17)

> Landed without the EnTT registry the original design assumed (§7.1's
> `ScriptState` component / `ScriptPreTickSystem`/`ScriptPostTickSystem`
> classes don't exist — 3.1's generic registry wiring is still deferred, same
> as when 5.1's `ItemDropSystem` hit the exact same gap). Followed
> `item_drops.hpp`'s established precedent instead: a small hardcoded system
> (`PackRuntime::Impl::entities`, `src/script/pack_runtime.cpp`) replicated
> through `ServerSession::spawn_script_entity`/`set_script_entity_state`/
> `remove_script_entity` (`inc/vb/net/session.hpp`, mirrors `spawn_item_drop`
> exactly — another interest-grid entry, no new wire message, its own NetId
> range at `0x4000'0000` disjoint from players and item drops).

- [x] Per-instance Lua state: `self` is a plain Lua table (the spec's
      `ScriptState`, just not EnTT-component-backed) created by
      `vb.world.spawn(kind, pos)` and stored in `PackRuntime::Impl::entities`
      keyed by NetId — persists across every `on_tick` call for that
      instance (verified in the integration test below by accumulating a
      counter on `self` across ticks), so two spawned objects of the same
      kind track independent data.
- [x] `on_spawn`/`on_tick`/`on_hit`/`on_death` wired: `on_spawn` fires once
      from `vb.world.spawn`; `on_tick` fires once per `PackRuntime::dispatch_tick`
      per live instance (`Impl::dispatch_entity_tick`); `on_hit` fires from a
      new `self:damage(amount, cause)` (notification-only — the engine
      tracks no generic-entity health, same "engine takes no position"
      posture as the still-unbuilt 6.5); `on_death` fires from a new
      `self:remove(cause)`, which also despawns (interest-grid removal +
      map erase) right after.
- [x] Decided: arbitrary fields for custom data, accessors for engine-owned
      state — `self` is a plain table (`self.hp = 10` just works) whose
      metatable's `__index` points at a shared `entity_methods` table
      (`get_pos`/`set_pos`/`get_kind`/`damage`/`remove`), so both coexist
      unless a pack picks a method's exact field name.
- [x] Test: `pack_runtime_integration_test.cpp` — real `ServerSession`/
      `ClientSession`/`LoopbackTransport`, spawn via chat command, asserts
      `self` state persists and grows across many ticks (not rebuilt per
      call), `on_hit`/`on_death` fire with the right args, and the instance
      both replicates to a client while alive and disappears from
      `remote_entities()` after `self:remove()`.
- [ ] Not done (out of scope for this item): no automatic despawn-on-health
      trigger (no health primitive exists for generic entities at all, only
      the notification hook) — a pack wanting mob HP tracks it itself on
      `self` and calls `self:remove()` when it hits zero. No client-side
      kind-specific rendering yet (`EntityKind` id is threaded through to
      replication but nothing branches on it, same pre-existing limitation
      `world::kItemDropKind` has).

### 6.2 Fully Lua-defined, immediate-mode reactive UI  ✅ (2026-09-17)

- [x] Reframed `ui.define(name, render_fn)`: `render_fn(state)` now runs
      once per UI frame for as long as the screen is open
      (`UiRuntime::render_frame()`, `src/script/ui_runtime.cpp`), not once at
      `open()` time. `open()` now only resolves the render function and
      seeds `state` (a persistent `sol::table`, built once from `ctx_json`)
      — it no longer evaluates anything itself, so there's exactly one
      evaluation per frame, not a double-evaluation on the opening frame.
      `main.cpp`'s existing per-frame UI block calls `render_frame()` right
      before `UiRenderer::draw(...)`, same call site as before, no new
      per-frame plumbing needed since `raygui` was already immediate-mode.
      The per-frame call is wrapped in `vm.begin_call_budget()`, same sandbox
      guard `report_click`/`report_change` already used for one-shot
      callbacks, now covering a function invoked continuously.
      **Client-VM-only change, no protocol/wire changes** — `C2S_UiEvent`/
      `S2C_OpenUi` are untouched; event handlers still call
      `ui.send_event`/`ui.close` exactly as before.
- [x] `content/base/ui/{inventory,pause}.lua` rewritten to the `render(state)`
      shape (parameter renamed `ctx` → `state`). `inventory.lua` also
      demonstrates real local reactivity: the list's `on_change` sets
      `state.selected` (no `ui.send_event`, purely cosmetic per the design),
      and the title label reads it back — proving a handler-mutated `state`
      value shows up on the very next frame with no reopen and no server
      round-trip.
- [x] Event handlers unchanged: `on_click`/`on_change`/`on_close` still
      route through `ui.send_event`/`ui.close` → the existing
      `C2S_UiEvent` path; only purely cosmetic state moved onto the
      persistent `state` table.
- [x] Tests (`tests/unit/ui_runtime_test.cpp`) rewritten to the new
      contract, including a new case that is the actual reactivity claim:
      a button's `on_click` increments `state.count`, a label renders it;
      `report_click` + a second `render_frame()` call show the label
      updated with no `open()`/reopen in between.

### 6.3 Server-side player-input interception, closed-schema custom keybinds  ✅ (2026-09-18, rate limiting still deferred)

- [x] `vb.register_keybind(name) -> index` at pack load — idempotent by name
      (same linear-scan-by-name shape as `register_entity`), rejected after
      `PackRuntime::freeze()`, capped at 32 registrations
      (`S2CKeybindRegistry::kMaxKeybinds`) so the bitset always fits one
      `InputCmd.keybinds` `u32` — a deliberate scope cap, same spirit as the
      existing `kMaxCmds`/`kMaxBlockRegistryRecords` guards.
- [x] Registered set synced at handshake as `S2C_KeybindRegistry` (47),
      wire shape mirrors `S2C_BlockRegistry` exactly: sent in the same
      `kAwaitingReady` step, `HandshakeServerHost::keybind_registry` hook
      (`nullopt` default = no frame, zero behavior change), wired by the new
      `PackRuntime::install_keybind_registry(host)` (`src/server/main.cpp`
      calls it right next to `install_join_veto`). Client applies it
      unconditionally in `ClientSession::tick()`, same reasoning as block
      registry (no cross-lane ordering guarantee vs. `JoinAccept`).
      `kEngineProtocolVersion` bumped 11 → 12.
- [x] Movement input (`PlayerInput`'s existing fields) untouched — additive
      `InputCmd.keybinds: u32` field alongside the existing `buttons: u8`.
- [x] `vb.on("player_input", handler)` fires inside `ServerSession::
      handle_input_batch`'s existing per-cmd loop (no `IngestInputSystem`/
      `MovementIntegrationSystem` split exists yet — §7.2's system runner is
      still `[ ]` — so this hooks the one function that loop already lives
      in, before its `physics::step_movement` call). `input` is
      `{move,yaw,pitch,buttons={...},keybinds={[name]=bool,...}}` (only
      registered names ever appear as `keybinds` keys); `return false`
      vetoes, a returned table overrides only the fields present (chained
      across multiple handlers in registration order, first veto wins).
      **Judgment call on veto semantics** (not spelled out in the spec):
      a veto drops the cmd's effect on movement/rotation entirely, but
      `input.last_seq` still advances so the cmd is durably consumed/acked
      instead of being silently reprocessed every batch forever — a literal
      "never happened, never acked" veto would desync client-side
      prediction/reconciliation with no way to converge.
      Only installed (`ServerSession::set_input_handler`) when a pack
      actually registers a `player_input` handler, so the hot per-tick input
      path pays zero extra cost otherwise (same conditional-install pattern
      6.6 used for `set_respawn_handler`).
      Tests: `tests/unit/pack_runtime_integration_test.cpp` (veto freezes
      authoritative position bit-exact; a chained replace reaches a second
      handler with only the overridden field changed); `tests/unit/
      pack_runtime_test.cpp` (idempotency/cap/freeze); `tests/unit/
      block_registry_test.cpp` (registry reaches a joined client / host
      that never opts in leaves it empty); protocol round-trip + lane tests.
- [ ] Per-connection rate limiting on custom-keybind events, defense in depth
      on top of the closed schema — **not implemented this pass**; still
      folds into the already-tracked, still entirely unimplemented
      "per-player rate limit / flood guard belongs with `GnsTransport`" item
      (Phase 1.3). The closed-schema bitset itself remains the primary flood
      defense the spec calls out.

### 6.4 Generic per-key persistent storage (script-owned identity/auth)  ✅ (2026-09-18)

- [x] `vb.db.get(key)` / `vb.db.set(key, value)` / `vb.db.delete(key)` —
      arbitrary script-chosen keys (`"user:" .. name`, `"session:" .. token`,
      ...), distinct from the existing pack-global `vb.storage`. The engine
      has no concept of "logged in" — a connection stays just a connection
      (as today) until a pack's own login flow looks up a record and decides
      to recognize it. Joining a world isn't authenticating, the same way
      loading a webpage isn't.
- [x] Storage backend: **not SQLite** — `vb::script::ScriptDb`
      (`inc/vb/script/db.hpp`/`src/script/db.cpp`) instead, one file per key,
      content-addressed by `sha256(key)` under a 2-hex-prefix shard
      directory (`<content_pack>/db/<prefix>/<hash>`), reusing
      `ClientAssetCache`'s on-disk shape (§4.4) rather than adding a new
      dependency. No list/enumerate — spec only calls for get/set/delete by
      an already-known key, and this scales to "one record per identity"
      fine without one. A real embedded-database swap (SQLite still the
      leading candidate if this ever needs range queries or transactions)
      stays a pure implementation-detail change behind the same interface,
      per this item's original "implementation detail, not a design
      blocker" framing.
- [x] `vb.crypto.hash(data)` — SHA-256 hex digest (`vb::core::sha256_hex`,
      `inc/vb/core/sha256.hpp`/`src/core/sha256.cpp`, dependency-free) so
      packs implementing their own login don't roll credential hashing in
      pure Lua — the sandbox strips `os`/`io` deliberately (§10.2), and
      pure-Lua hashing is slow and easy to get wrong. The engine still takes
      no position on auth as a concept — see `ARCHITECTURE_SPEC.md` §19 Q6.
      Also used internally by `ScriptDb` for its key-to-filename hashing.

### 6.5 Shared block-damage breaking (default + override crack texture) ✅ (2026-09-18, crack rendering deferred)

- [x] `BlockType` gains `max_damage` (0 = today's instant break, the
      default — no behavior change for any existing block); `vb.register_block{...}`
      exposes it (`def.max_damage`, `inc/vb/world/block.hpp`,
      `src/script/pack_runtime.cpp`). No `crack_texture` field added — there's
      nowhere to put it yet (`BlockType` has no texture/model fields at all
      until 4.3/5.1's real atlas system lands), so it stays deferred alongside
      the rendering item below rather than added unused.
- [x] Sparse server-side damage map: `vb::world::BlockDamageSystem`
      (`inc/vb/world/block_damage.hpp` + `src/world/block_damage.cpp`), a
      pure `pos -> {damage, max_damage, last_touched_tick, contributors}` map
      holding only entries with damage > 0 — same posture as `ItemDropSystem`
      (no net/script dependency, unit-testable standalone,
      `tests/unit/block_damage_test.cpp`). Does not touch chunk revisions or
      mesh invalidation on its own.
- [x] `C2S_BlockBreakBegin{pos, face}` / `C2S_BlockBreakStop{pos}`
      (`inc/vb/protocol/world.hpp`, message ids 48/49; `BlockRegistryRecord`
      also gains `max_damage`, `kEngineProtocolVersion` 12 → 13) bracket a
      player holding a target;
      `ServerSession::handle_block_break_begin` gates entry with the same
      reach check `apply_block_edit` uses (`WorldReplicator::in_reach`, new)
      plus an engine-level `max_damage > 0` check, then
      `vb.on("block_break_begin", ...)` (vetoable) via
      `ServerSession::BlockBreakHooks::begin`.
- [x] `vb.on("block_break_tick", handler)` fires once per tick **per
      contributing player** (`ServerSession::update_block_damage`, called
      from `tick()` alongside `update_item_drops`) — returns the damage delta
      to add; multiple concurrent contributors ("breaking together") sum,
      and so do multiple registered handlers for the same call (an
      orthogonal case the spec didn't call out, summed the same way rather
      than picking one arbitrarily). No handler registered = zero built-in
      policy, damage never accrues.
- [x] `vb.on("block_health_tick", handler)` fires once per tick **per
      damaged block**, regardless of contributors — `(pos, damage,
      max_damage, ticks_since_last_hit)` in, a replacement damage value (or
      nothing = unchanged) out; the last handler to return a number wins if
      several are registered. No handler registered = permanent damage, no
      healing at all.
- [x] Completion (summed damage reaches `max_damage`) drives the *existing*,
      unchanged `C2S_BlockEdit`/`WorldReplicator::apply_block_edit`/`on_break`
      pipeline via a synthesized `C2S_BlockEdit{kBreak}` attributed to
      whichever player was contributing when it completed (arbitrary among
      concurrent contributors) — this system only gates *when* that fires,
      never replaces it.
- [ ] **Deferred, not attempted:** no wire message replicates the damage
      *value* itself to nearby players yet (the spec's "ride the existing
      interest/replication system... a transient interest-managed record"
      design) — scoped down this session to just the begin/stop/complete
      mechanism, since the only consumer of a replicated damage value is the
      crack overlay below, which is itself blocked. `BlockDamageTickResult::
      changed`/`cleared` already exist and are ignored by
      `ServerSession::update_block_damage` for exactly this reason — wiring
      them up is the natural next step once there's a client to show them to.
- [ ] Default generic crack overlay (progressive stages by damage ratio) +
      `crack_texture` override: **still blocked on** the still-pending real
      texture/atlas system (4.3/5.1 — client is untextured cubes today), same
      as before this session. No client UI sends `C2S_BlockBreakBegin`/`Stop`
      yet either (`ClientSession::send_block_break_begin`/`send_block_break_stop`
      exist as a real, tested wire API — `src/client/main.cpp`'s existing 5.2
      hold-to-break timer is untouched and still governs every
      `max_damage == 0` block, which is every block in `content/base` today).

### 6.6 Player damage & death (foundational — split out from the rest below) ✅

> Audited 2026-09-17: `void_kill_y` (`inc/vb/core/config.hpp:31`) was
> the only damage source in the entire engine — no fall damage, no PvP, no
> mob damage, no hunger/starvation. This was upstream of combat, PvP, and
> hazards, so it was called out on its own rather than folded into the rest
> of 6.7-6.13 — most of that later content still depends on this having
> landed first.

- [x] A generic damage primitive: `player:damage(amount, cause)`
      (`PlayerHandle::damage`, `src/script/pack_runtime.cpp`) →
      `ServerSession::damage_player(NetId, float, string_view)`
      (`src/net/session.cpp`). `cause` is an opaque string threaded through
      unchanged to the respawn hook below — the engine takes no position on
      what "fall"/"pvp"/"void" mean. The void-kill check itself is now just
      another `apply_damage()` call with `cause = "void"`, not a separate
      code path.
- [x] `check_respawns()` no longer hardcodes the outcome: it fires
      `ServerSession::set_respawn_handler(fn(NetId, cause, health_before) ->
      RespawnDecision{heal_to, pos, message})` once health reaches 0 (any
      cause, void included) and applies whatever it returns. Unset (e.g.
      `--singleplayer` before any `PackRuntime` attaches one) falls back to
      the original behavior byte-for-byte — full heal, teleport to the join
      spawn point, the same `"* you died and respawned"` line — so every
      pre-6.6 caller/test (`netcode_test.cpp`'s void-kill test included)
      passes unmodified. `PackRuntime::attach_session` installs a real
      handler only when a pack actually registered
      `vb.on("player_death", ...)` (checked once, after `freeze()`, so every
      pack file has already had a chance to register).
      Inventory-drop is the *handler's* business, not the primitive's: a
      `drop_inventory = true` in the returned table spawns every slot as a
      real dropped-item entity (Phase 5.1's `ItemDropSystem`, reusing
      `spawn_item_drop`) at the player's position **at time of death**, not
      wherever they're about to respawn — dropping at the new respawn point
      instead was tried first and immediately self-picked-up by
      `ItemDropSystem`'s pickup radius since the player is standing right on
      it; caught by the new integration test below, not just reasoned about.
- [x] Spawn point: `check_respawns()`'s hardcoded reuse of the join spawn
      point is gone; `ServerSession::spawn_point(NetId)` exposes the join
      point as a value a respawn handler can read (e.g. to keep the old
      behavior on purpose), not something it's stuck with. A real
      bed/checkpoint pack feature still needs pack-side storage (`vb.db`,
      6.4, not yet landed) to remember a chosen point across respawns —
      *this* item was only about the engine no longer forcing one point.
- [x] No PvP toggle: still N/A, same reasoning as before — now meaningful to
      add once a pack actually calls `player:damage` on another player, not
      before.
- [x] Tests: `netcode_test.cpp`'s existing void-kill/respawn test passes
      unmodified (proves the no-handler-set fallback is byte-identical).
      New: `pack_runtime_integration_test.cpp` — `player:damage()` +
      `vb.on("player_death", ...)` end-to-end over a real
      `ServerSession`/`ClientSession`/`LoopbackTransport`: custom heal
      amount, custom respawn position, custom chat message, and
      `drop_inventory = true` actually dropping+clearing inventory (and not
      self-repicking-up) all asserted through the real wire messages a
      client receives.
- [ ] Not done, left for whichever pack/phase actually needs it: fall
      damage, PvP, mob damage, hunger — this item only adds the *primitive*
      (`player:damage`) and the *decision hook* (`player_death`); no content
      calls either yet (same "mechanism before content" posture as every
      other Phase 4/5 item). `content/base` has no `death.lua` — nothing
      currently overrides the built-in fallback in the shipped base pack.

### 6.7 Physics / movement parameters (default + override) ✅ (global override; per-entity-kind deferred)

- [x] `vb.physics.set_params{...}` (`src/script/pack_runtime.cpp`) lets a
      pack override any `physics::MoveParams` field (gravity, walk/sprint
      speed, accel, friction, jump speed, step height, fly speed, ...).
      **Scoped to one global override, not per-entity-kind:** no entity kind
      besides the player runs `step_movement` today (script entities from
      6.1 have no physics at all), so a per-kind table would have nowhere
      else to apply — revisit if/when a non-player kind gets real physics.
      `PackRuntime::effective_move_params(base)` applies only the fields the
      pack actually set on top of `base`, leaving the rest untouched.
- [x] `ServerConfig.gravity` reconciliation, decided: it's the *base* fed
      into `effective_move_params()` (`move_params.gravity = config.gravity`
      in `src/server/main.cpp`, before the pack override runs) — an
      operator's `server.toml` sets the engine default, a pack's explicit
      `vb.physics.set_params{gravity=...}` wins over it if set. Documented
      inline at the call site, not just here.
- [x] **Also closed, not originally scoped but found while replicating
      this:** the client's local prediction (`ClientSession::move_params_`)
      never received the server's `MoveParams` at all before this — every
      client (dedicated-server and `--singleplayer` alike) silently
      predicted with `physics::MoveParams{}`'s own hardcoded defaults
      regardless of `ServerConfig.gravity` or any future pack override,
      correctness relying entirely on reconciliation snapshots papering
      over the drift. New `S2C_MoveParams` (id 50, Phase 6.7,
      `kEngineProtocolVersion` 13 → 14) sent between `C2S_Ready` and
      `S2C_JoinAccept` alongside `S2C_BlockRegistry`/`S2C_KeybindRegistry`
      (`HandshakeServerHost::move_params`, `nullopt` default = no frame,
      zero behavior change) fixes this for both the dedicated server and
      `--singleplayer`'s in-process host.
      **Also found and fixed while wiring the client:** two call sites in
      `src/client/main.cpp` (`run_headless` and the windowed `enter_playing`)
      constructed a fresh default `physics::MoveParams` and called
      `client->set_move_params()` with it *after* join, unconditionally
      stomping whatever `S2C_MoveParams` had already applied moments
      earlier (it arrives in the same handshake step as `JoinAccept`). Fixed
      by reading back `client->move_params()` (new getter,
      `inc/vb/net/session.hpp`) instead of reconstructing a default.

### 6.8 Day/night cycle curve (default + override) ✅

- [x] `vb::world::DayNightCurve` (`inc/vb/world/daynight.hpp`) generalizes the
      old fixed 4-keyframe gradient into a `vector<DayNightKeyframe>` (tick +
      brightness + color); `sky_brightness()`/`sky_color_for_time()` gain
      curve-taking overloads (an empty curve falls back to
      `default_day_night_curve()`, which reproduces the original 4 keyframes
      exactly — every pre-6.8 call site/test is unaffected).
      `vb.daynight.set_curve{keyframes = {{tick=, brightness=, color={r,g,b}},
      ...}}` (`src/script/pack_runtime.cpp`) lets a pack override it; replicated
      to joining clients as `S2C_DayNightCurve` (51, `kEngineProtocolVersion`
      14 → 15) between `C2S_Ready` and `S2C_JoinAccept` alongside
      `S2C_BlockRegistry`/`S2C_MoveParams` (`HandshakeServerHost::
      day_night_curve`, `nullopt` default = no frame, zero behavior change).
      `src/client/main.cpp`'s sky-clear code reads `client->day_night_curve()`
      instead of the bare default overload.
- [x] `day_length_seconds` wired to both a config surface and a Lua surface,
      closing the second half of the gap this item called out:
      `ServerConfig::day_length_seconds` (`server.toml`, default 1200.0,
      matching `ServerSession`'s own hardcoded default exactly — extracted to
      a shared `vb::net::kDefaultDayLengthSeconds` constant so the two never
      drift) is the base; `vb.daynight.set_day_length(seconds)` overrides it
      on top (rejects `seconds <= 0`), the same config-then-pack-override
      shape 6.7's `gravity`/`vb.physics.set_params` established. Wired in both
      `src/server/main.cpp` (`config.day_length_seconds` base) and
      `--singleplayer`'s `Singleplayer` struct (`kDefaultDayLengthSeconds`
      base, no `server.toml` there) via `PackRuntime::
      effective_day_length_seconds(base)`.
- [ ] Not done, deliberately out of scope: a pack-supplied arbitrary *curve
      function* (Lua callback re-evaluated every read) — only data-driven
      keyframes, matching every other Phase 6 "default + override" item's
      shape (a table of values, not an executable hook) and avoiding a
      per-frame Lua call from the replication path. A pack wanting a
      non-piecewise-linear shape can still approximate it with more
      keyframes.

### 6.9 Inventory stacking (default + override) ✅

- [x] `world::kDefaultMaxStackSize` (64, `inc/vb/world/block.hpp`) + a new
      `BlockType::max_stack` field (default = that constant) —
      `vb.register_block{max_stack = N}` overrides it per block, same
      def-parsing shape as 6.5's `max_damage`. Not `register_item`: every
      holdable item is already a registered block (see
      `content/base/blocks/planks.lua`'s comment on why `register_item`
      never allocates its own id space), so that's where the override
      belongs today.
      A new `PackRuntime::Impl::give_item(id, item, count)` is the one place
      that actually adds to an inventory: fills existing under-cap slots for
      that item first, then starts as many new slots as needed for the
      remainder (each capped at `max_stack`). Both `player:give()` and the
      item-pickup handler (`attach_session()`, previously two separate
      `push_back` call sites) now call it, so picking a dropped item up
      stacks identically to a script handing it to you — closing a
      duplication the existing pickup-handler comment already claimed ("credits
      their inventory exactly like give() does") but the code didn't actually
      guarantee.
      No slot-count cap on the inventory itself — re-reading the item that
      opened this task, "stack cap ... and slot count" reads as one thing
      (how much fits in one slot), not a second cap on total slots; no
      evidence elsewhere of an intended max-slots limit.

### 6.10 Chat transform/moderation hook ✅ (2026-09-18, rate limiting still deferred)

- [x] `ServerSession::ChatHookResult{veto, replacement_text}` +
      `set_chat_handler(std::function<ChatHookResult(NetId, string_view)>)`
      (`inc/vb/net/session.hpp`) replace the old bool-veto-only handler type.
      `PackRuntime::dispatch_chat` now returns `ChatHookResult` (was `bool`);
      `PackRuntime::Impl::run_chat` chains every `vb.on("chat", handler)` in
      registration order — `return false` vetoes (first veto wins, same as
      `run_veto`), `return "text"` replaces what the *next* handler (and
      ultimately the broadcast) sees, `true`/`nil`/anything else passes the
      current text through unchanged. Same shape as `run_player_input`/
      `InputHookResult`, just one string field instead of a table.
      `handle_chat()` (`src/net/session.cpp`) applies the veto/replacement
      before formatting `"name: text"` and broadcasting `S2C_Chat`. No wire
      message changed — this is purely a server-side hook contract change,
      no `kEngineProtocolVersion` bump needed.
      Rate limiting still not implemented — a pack can build one on top of
      this hook (e.g. via `vb.db`/`vb.storage` timestamps), but the engine
      doesn't enforce one itself, matching the item's own framing ("or its
      own rate-limit policy instead of the engine's none-at-all").

### 6.11 Item drop parameters (default + override) ✅ (2026-09-18)

- [x] `ItemDropSystem::spawn()` now takes optional per-drop
      `pickup_radius`/`lifetime_seconds` overrides (`inc/vb/world/item_drops.hpp`);
      unset falls back to the system's own construction-time defaults (1.5 /
      120.0), byte-identical to every pre-6.11 caller. `BlockType` gains
      `pickup_radius`/`drop_lifetime_seconds` (both `-1.0` = "no override" —
      0 is a plausible real radius, so it can't double as the sentinel).
      `vb.register_block{pickup_radius=..., item_lifetime_seconds=...}`
      (`src/script/pack_runtime.cpp`) sets them; not `register_item` as
      originally sketched, same reasoning 6.9 already established — every
      holdable item is a registered block today.
      `ServerSession::spawn_item_drop` (`src/net/session.cpp`) looks the
      dropped item's id up in the live `WorldReplicator`'s registry and
      passes any override through; no `replicator_` (a bare test harness) or
      an unknown id both fall through to the engine defaults unchanged.
      Tests: 1 new `item_drops_test.cpp` case (a wide `pickup_radius`
      override collects from outside the system default; an overridden
      `lifetime_seconds` survives well past the system default) + 1 new
      `pack_runtime_integration_test.cpp` end-to-end case (a
      `register_block{pickup_radius=10}` item is picked up 8 blocks away, well
      outside the 1.5 default, over a real `ServerSession`/`ClientSession`/
      `LoopbackTransport`). Full `vb_tests` green (248/248) + all 4 CTest
      cases pass on `build-net-lua`.

### 6.12 Entity animation clip priority (cosmetic, low priority) ✅ (2026-09-18, no code change — decision already matches implementation)

- [x] `resolve_anim_clip()`'s fixed priority order (`kDead > kHurt > kActing
      > kJump/kFall > kRun > kWalk > kIdle`) and its speed thresholds
      (`AnimThresholds`, `inc/vb/render/entity_visual.hpp:44-48`) stay
      engine-fixed even once per-kind clip *assets* are pack-defined
      (already tracked separately under 4.2's `visual = {...}` sub-table) —
      *which* clip wins in a given state is a distinct, finer-grained
      concern. Cosmetic only; lowest priority in this section.
      Verified 2026-09-18: `resolve_anim_clip()` (`inc/vb/render/
      entity_visual.hpp:51-`) already implements exactly this fixed order
      with no Lua hook of any kind — this item was a "leave it engine-fixed"
      design decision that the code already matched, not a pending
      implementation. No change made.

### 6.13 Read-only server config visibility (not a pack-override surface) ✅ (2026-09-18)

- [x] `ServerConfig` (`tick_rate`, `view_distance`, `max_players`,
      `void_kill_y`, ...) are server-**operator** settings
      (`server.toml`/CLI), a different persona from a content-pack author —
      a pack should not be able to silently change `max_players` out from
      under the operator running the server. At most, expose a read-only
      `vb.config.get(key)` so a pack can *react* to these values (e.g. tune
      spawn density to view distance), not a full override registry like
      6.6-6.11 above.

### 6.14 Lua-driven worldgen pipeline (FastNoise2 backend) ✅ (2026-09-18)

> Moved here from Phase 4.2 (2026-09-17) — it's the same "override an
> engine default from a pack" shape as the rest of Phase 6, and `biomes`
> was already named in this phase's own intro note as one of the four
> systems in scope. World generation (biome selection, height params, block
> choice) is currently 100% hardcoded in `WorldGenerator::generate`
> (Phase 2's fBm heightmap pipeline); `vb.worldgen.set_pipeline` doesn't
> exist. FastNoise2 (`v0.10.0`, `VB_WITH_WORLDGEN`) has been a pinned
> dependency since Phase 0 but nothing constructs a node graph with it yet
> — the hand-rolled integer-hash noise in `vb/core/noise.hpp` is what
> `WorldGenerator` actually uses today.

- [x] `vb.worldgen.set_pipeline(fn)` — a pack-supplied stage function (or
      ordered list of stages) that replaces `WorldGenerator::generate`'s
      hardcoded body; falls back to the current hand-rolled fBm heightmap
      when no pack sets one, so an unmodified `content/base` keeps working.
      **Shipped as `set_pipeline(table)`, not `set_pipeline(fn)`** — Lua/
      sol2 is strictly single-threaded and `WorldGenWorkerPool` calls
      `generate()` from N worker threads with zero locking, so a literal
      per-chunk Lua callback was never viable; the table is compiled once,
      main thread, into an immutable `worldgen::PackWorldGenPipeline`. See
      `docs/lua-api.md`'s `vb.worldgen.set_pipeline` entry for the full
      writeup and `STATE.md` for the session notes.
- [x] Expose FastNoise2 node-graph construction to Lua (`vb.noise.*`) —
      the natural backend for pack-defined pipelines; the hand-rolled
      `vb/core/noise.hpp` path stays as the deterministic zero-dependency
      default (`VB_WITH_WORLDGEN` off), same posture as every other
      `VB_WITH_*`-gated optional backend. `vb.noise.*` builds a small
      portable node-graph IR (`vb/worldgen/noise_graph.hpp`'s `NoiseNode`)
      that compiles to either evaluator depending on the build flag —
      confirmed working end-to-end under `VB_WITH_WORLDGEN=ON` this session
      (real FastNoise2 v0.10.0-alpha fetched, linked, and exercised by the
      full test suite; see STATE.md). Fixed a real pre-existing bug found
      while wiring this: `cmake/Dependencies.cmake` pinned FastNoise2 to a
      tag (`v0.10.0`) that doesn't exist in `Auburn/FastNoise2` — the real
      tag is `v0.10.0-alpha` — unnoticed until now because nothing had ever
      actually fetched/built it before this item.
- [x] Biome selection (`ARCHITECTURE_SPEC.md` §6 stage 2): Voronoi-cell
      partitioning with adjacency-weighted probability (WFC-flavored,
      non-backtracking — design finalized 2026-09-17) reads `register_biome`
      entries (already captured by `PackRuntime`, 4.2) instead of nothing.
      `vb/worldgen/biome_selector.hpp`'s `BiomeSelector` implements this;
      deliberately **not** globally memoized/locked (recomputes its bounded
      neighbor recursion from scratch per query, trading cache-hit-rate for
      zero shared mutable state across worker threads) — see that header's
      own comment for the reasoning, and its Deferred-section entry below
      for the follow-up if profiling ever shows this matters.
- [x] Carvers + vein/scatter (new stage 5, ore/valuable-block placement) +
      decoration pass — each a pipeline stage a pack can plug in via the
      same `set_pipeline` mechanism. **Decoration scope narrowed to
      schematic-only** (pure data: a block-offset list scattered per chunk),
      not the spec's "procedural callbacks" — same threading reasoning as
      `set_pipeline` itself; see the Deferred section below.
- [x] Determinism gate (`tests/unit/worldgen_test.cpp`'s golden-hash test)
      needs a pack-driven pipeline case once this lands, alongside the
      existing hardcoded-pipeline golden. Added
      ("worldgen determinism gate, pack-driven pipeline (golden value)"),
      built directly against `worldgen::NoiseNode`/`PackWorldGenPipeline` in
      C++ (not through Lua) so it stays backend-evaluator-pinned (always the
      hand-rolled path) regardless of whether a given build links real
      FastNoise2 — the fixed-default-path golden test is completely
      untouched (byte-identical output confirmed both with and without
      `VB_WITH_WORLDGEN`).

### 6.15 Example Lua script demonstrating the Phase 6 "default + override" features ✅ (2026-09-18)

> Blocked on the rest of Phase 6 (6.9–6.14) landing — the "Lua overhaul": once
> every default-with-a-pack-override surface this phase introduced (entity
> kinds, reactive UI, custom keybinds, `vb.db`/`vb.crypto`, block damage,
> player damage/death, physics params, day/night curve, inventory stacking,
> chat transform, item-drop params, worldgen pipeline) is in place, write one
> `content/base`-sibling example pack that actually exercises them together,
> not just in isolation across scattered unit tests.

- [x] A standalone example content pack (e.g. `content/examples/kitchen_sink`
      or a `content/base/examples/` script loaded only in a demo mode) that
      calls each Phase 6 API at least once with a visible, in-game effect: a
      custom entity kind with `on_tick`/`on_hit`/`on_death`, a `ui.define`
      screen opened via `player:open_ui`, a custom keybind bound to an action,
      `vb.db`/`vb.crypto.hash` used for a small persistent counter, a
      `max_damage` block with a `block_break_tick` handler, a
      `player_death` handler with custom respawn/drop behavior,
      `vb.physics.set_params` tuning movement, `vb.daynight.set_curve`/
      `set_day_length` for a non-default sky, and (once 6.9-6.11 land) a
      stacking item, a chat filter, and tuned item-drop params.
      Shipped as `content/examples/kitchen_sink/` (the first-listed option).
      `entities/sentry.lua`'s `on_tick`/`on_hit`/`on_death` are real and
      dispatched (Phase 6.1, landed 2026-09-17 — no EnTT registry involved,
      still doesn't exist, Phase 3.1) — a `/sentry` chat command spawns,
      hits, and kills one for real. Corrected two stale docs found while
      verifying this: `content/base/entities/dropped_item.lua`'s own comment
      and `docs/lua-api.md`'s worked-example table both still claimed
      `vb.world.spawn` "just logs and returns nil", predating 6.1 — fixed
      both. Every 6.15 bullet has a real, running effect, confirmed by
      `tests/unit/kitchen_sink_pack_test.cpp` and a manual
      `voxel_browser_server --content-pack content/examples/kitchen_sink` run
      this session. Also folds in Phase 6.14's own worked-example gap
      (`vb.worldgen.set_pipeline` + `vb.register_biome` with real
      `probability`/`adjacency`, plus a carver and a vein) since 6.14 landed
      earlier this same session.
- [x] Should double as living documentation: comment each block with which
      `REMAINING_TASKS.md` phase/`docs/lua-api.md` section it demonstrates, so
      it stays a working reference alongside the prose docs rather than
      drifting out of sync with the actual API surface. `init.lua` carries a
      file-by-file index; every individual file's own header comment names
      the exact phase/doc section and, where relevant, contrasts with how
      `content/base` already demonstrates a *different* facet of the same
      API (e.g. `chat.lua`'s text-rewriting vs. `crafting.lua`'s veto-only
      chat use).
- [x] Not `content/base` itself — base pack content should stay minimal/
      production-shaped (spec §5.1); this is a separate, clearly-labeled demo
      pack an operator can point `--content-pack` at, or a devs-only mode, not
      something a real server loads by default. `content/base` itself is
      completely untouched by this item.

### 6.16 Client-local HUD mechanism (engine raw state, Lua presentation) ✅

> User-requested (2026-09-18): the hold-to-break progress bar added in 5.2
> was pure hardcoded C++ (`DrawRectangle` calls in `src/client/main.cpp`),
> which broke the project's "engine provides raw state, Lua deals with
> presentation" rule as much as anything in the codebase. Closing that gap
> needed a real mechanism first — `UiRuntime`'s existing `ui.define`/`open`/
> `close` model is for server-pushed modal screens (inventory, pause), not an
> always-on overlay — so this added one. **Revised same day, also
> user-requested:** the first pass added a `kProgressBar` widget type, which
> the user correctly called out as still baking a presentation *concept*
> into the engine (Lua only supplied the value, not the drawing). Replaced
> with a meaning-free `kRect` primitive — see the second bullet below.

- [x] `ui.define_hud(render_fn)` (`vb::script::UiRuntime`, distinct from
      `ui.define`'s named-screen registry): registers a single render
      function that's evaluated every UI frame unconditionally, independent
      of whatever modal screen `open()`/`close()` currently has up. Its own
      persistent `state` table (Phase 6.2's reactivity mechanism) is created
      once and never reset by a modal screen opening/closing alongside it.
      `UiRuntime::render_hud()` evaluates it and returns the widget list;
      `vb::render::UiRenderer` draws it exactly like a modal screen's widgets
      (a *second* `UiRenderer` instance, since one shared instance would
      thrash its per-widget-id text/list edit caches by seeing the drawn
      `ui_name` toggle between "hud" and the modal name every frame).
- [x] New `kRect` widget type — a raw filled rectangle (`fill_r/g/b/a`) with
      an optional 1px outline (`border_r/g/b/a`, alpha 0 = none), drawn with
      plain `DrawRectangle`/`DrawRectangleLines`, no raygui control involved.
      Deliberately the only widget type with **no semantic meaning at all**
      — not "a progress bar", not "a health bar", just a box at `(x,y,w,h)`.
      A pack composes whatever purely-visual element it wants (a progress
      bar is two of these: a background/border rect, and a fill rect sized
      by a fraction) entirely in Lua; the engine never bakes in what the
      rectangle *represents*. First new widget type since Phase 4.5/6.2
      shipped label/panel/button/textbox/list.
- [x] Raw client-local state exposed read-only to the UI Lua VM as `client.*`
      (new top-level table, `src/script/ui_runtime.cpp`) — nothing here draws
      a pixel, it's queried by whatever a HUD's `render_fn` chooses to show:
    - `client.break_progress()` — nil, or 0..1 while holding to break a
      block. The hold-timer/reach/target-tracking *logic* stays engine-side
      (`src/client/main.cpp`, unchanged from 5.2) since it's gameplay input
      handling, not cosmetics; only the *drawing* moved to Lua.
    - `client.screen_size()` — `{width=.., height=..}`, since widgets take
      absolute pixel positions and a HUD centering something needs the real
      window size rather than a hardcoded guess.
- [x] `content/base/ui/hud.lua` (new): reads `client.break_progress()` and
      builds the bar from two `rect` widgets (background+border, and a fill
      whose width is `bar_w * progress`) — the exact visual the old
      hardcoded C++ produced, but every pixel of it (position, size, both
      colors, and the two-rectangle composition itself) is a Lua-side
      decision now, not an engine one.
- [x] **Bug found and fixed while wiring this in, not just the requested
      change:** `--singleplayer` never asset-syncs (no `PackRuntime`/manifest
      on that in-process path, 4.3's known gap), so `ui/*.lua` was never
      loaded there at all — every existing Lua UI screen (`base:pause`,
      `base:inventory`) was already silently dead in the most common dev/test
      path, not just the new HUD. Fixed in `src/client/main.cpp`'s
      `enter_playing`: singleplayer now reads `ui/*.lua` directly off
      `kSingleplayerContentPack` from disk (client and integrated server
      share one filesystem there, so there's nothing to "sync"); a real
      multiplayer connection is unaffected, still reading
      `client->virtual_pack_fs()`.
- [x] Verified live, not just by unit test: launched `voxel_browser.exe
      --singleplayer` (windowed), captured the mouse, held LMB on a block,
      and screenshotted mid-hold — the progress bar renders correctly at the
      expected position/fill, with zero "ui pack file failed to load" lines
      in the log. Unit tests: 4 new cases in `tests/unit/ui_runtime_test.cpp`
      (hud renders nothing until `client.break_progress()` is set, hides
      again on `nullopt`, hud `state` persists independent of a modal
      screen's open/close cycle, disabled-build stub no-ops cleanly).
- [ ] Display-only for now: hud widgets aren't wired to
      `report_click`/`report_change` — no interactive HUD element exists yet.
      A future one (e.g. a hotbar slot click) would need that wiring added.
- [ ] The player list / chat box / hotbar (`src/client/main.cpp`'s other
      always-on HUD elements, predating this) are still hardcoded C++,
      untouched by this item — only the break-progress bar was in scope.
      Migrating the rest to `ui.define_hud` (a "real" Lua HUD replacing
      draw_overlay entirely) is a natural, larger follow-up, not attempted
      here.

### 6.17 Movement/break-place bindings and hold-to-break timing as default + override

> User-requested (2026-09-18), after being surprised that (a) WASD/LMB-break/
> RMB-place are 100% hardcoded C++ with no pack involvement at all, and (b)
> the hold-to-break duration doesn't persist/heal across repeated attempts on
> the same block. Both trace back to the same root cause: `content/base`
> ships every block with `max_damage = 0`, so `src/client/main.cpp`'s
> original 5.2 hold-to-break timer — a fixed, local-only, non-authoritative
> `kBreakSeconds = 0.35` constant — is still what governs breaking for every
> block in the game today, and the real, already-built, already-overridable
> `vb::world::BlockDamageSystem` (6.5) never gets consulted at all. This is
> the exact gap 6.5's own last bullet already flagged ("the existing 5.2
> hold-to-break timer is untouched and still governs every `max_damage == 0`
> block, which is every block in `content/base` today") — this item is where
> that finally gets closed, plus the separate, never-before-tracked input-
> binding gap. Matches the project's stated core philosophy (`ARCHITECTURE_SPEC.md`
> §7/§17): engine ships a sane default, a pack can override as much of it as
> it wants, same shape as `vb.physics.set_params` (6.7) and
> `vb.daynight.set_curve` (6.8).

- [ ] **Give breaking a real, overridable default duration+retention+heal
      policy instead of nothing.** `block_damage.hpp` is explicit that the
      engine "ships zero built-in accrual/heal policy" — that's the actual
      bug the user hit ("I don't feel like the block is retaining the break
      value... duration is still the same across multiple attempts"): with
      no pack `block_break_tick`/`block_health_tick` handlers registered
      (true for `content/base`), and `max_damage == 0` on every block, there
      is no damage value at all to retain — every hold is an independent
      local timer, by design, not a bug in `BlockDamageSystem` itself.
      Two changes needed together:
    - Engine-level **default** `max_damage` (e.g. derived from a new
      `vb.blocks.set_break_defaults{seconds = 0.35, ...}` global, applied to
      any block that doesn't explicitly set its own `max_damage`) instead of
      today's implicit 0 — so out-of-the-box breaking already goes through
      `BlockDamageSystem`, not the parallel client timer.
    - Engine-level **default** `damage_tick_fn`/`health_tick_fn` policy
      (currently only ever pack-supplied): a straightforward "1/`seconds`
      damage per contributing-player-tick; after `heal_after_seconds` idle,
      heal back at `heal_rate` per tick" default, overridable exactly like
      today by registering `vb.on("block_break_tick"/"block_health_tick",
      ...)` (pack handler replaces the default entirely, same "no built-in
      policy once you opt in" semantics already documented for those hooks).
      `vb.blocks.set_break_defaults{...}` is the pack-facing knob for tuning
      the default without writing full tick handlers, mirroring
      `vb.physics.set_params`'s "override individual fields on top of a
      built-in default" shape.
- [ ] **Wire the client to the real system instead of the parallel timer.**
      `ClientSession::send_block_break_begin`/`send_block_break_stop`
      (6.5) already exist, tested, unused. Replace `src/client/main.cpp`'s
      local `breaking`/`break_target`/`break_progress`/`kBreakSeconds` block
      with: send `C2S_BlockBreakBegin` on first LMB-down over a voxel /
      `C2S_BlockBreakStop` on release-or-retarget, and read progress back
      from server-replicated damage state rather than a local clock — which
      needs the still-deferred "replicate the damage *value*, not just
      begin/stop/complete" half of 6.5 (`BlockDamageTickResult::changed`/
      `cleared`, currently computed and thrown away) finished as a
      prerequisite. `client.break_progress()` (6.16) keeps its exact same
      signature (nil | 0..1) so `content/base/ui/hud.lua` needs no changes.
      Placing (RMB, always instant) is unaffected.
- [ ] **Movement/action key bindings as a pack-overridable default**, not
      just a client-local rebind (5.3's still-"not attempted" keybindings
      screen is a *different*, complementary gap — physical-key-to-action
      storage/UI on one player's machine; this item is the pack/engine
      default those local rebinds would apply on top of). Today WASD
      (`sample_input_cmd`, `src/client/main.cpp:351-360`) and LMB-break/
      RMB-place (same file, the block-edit input block) are compiled-in
      constants with no pack seam at all — unlike literally every other
      Phase 6 system, a pack cannot change what triggers movement or
      breaking/placing. Proposed shape, following 6.3's existing
      `vb.register_keybind`/`S2C_KeybindRegistry` substrate rather than
      inventing a second mechanism: extend that registry to cover the
      engine's own built-in core actions (`move_forward`, `move_back`,
      `move_left`, `move_right`, `jump`, `sprint`, `sneak`, `break`,
      `place`) with their current hardcoded keys as the pre-registered
      defaults, so `vb.rebind_keybind("break", key)`-style pack overrides
      and (later, 5.3) a real settings-screen UI both write into the one
      registry instead of two separate ones. **Open design question, not
      resolved here:** whether core movement axes (continuous, analog-ish)
      fit the existing keybind registry's boolean-per-tick shape at all, or
      need their own parallel `vb.movement.set_bindings{...}` — decide
      during implementation, not speculatively here.
- [ ] Tests: extend `tests/unit/block_damage_test.cpp` for the new default
      policy (accrual without any registered handler, heal-after-idle
      without any registered handler, a pack override replacing just the
      default cleanly); a `pack_runtime_integration_test.cpp` case proving a
      `content/base`-equivalent pack (no handlers registered at all) now
      retains damage and heals over multiple attempts on one block; update
      `content_pack_test.cpp` if `content/base`'s shipped blocks' effective
      `max_damage` changes as a result.

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
  now so the decoration-pass design leaves room for it. **Phase 6.14 landed
  the dependency** (`vb.worldgen.set_pipeline` + `vb.register_biome`'s
  `decoration` schematic entries, `vb/worldgen/pipeline.hpp`'s
  `DecorationEntry`) — this item itself is still not attempted. Two
  narrower gaps 6.14 left inside what it *did* ship, worth folding into
  whichever future session tackles this:
  - **Procedural/callback decoration.** 6.14's decoration is schematic-only
    (a fixed block-offset list) — a per-site Lua callback (e.g. "grow a
    randomized tree shape") can't run on a `WorldGenWorkerPool` worker
    thread (Lua/sol2 is strictly single-threaded; see `set_pipeline`'s own
    entry in Phase 6 above for the same constraint). Would need either a
    main-thread deferred-apply pass after a chunk comes back from a worker,
    or a per-worker `sol::state`, neither attempted.
  - **Cross-chunk decoration.** 6.14's decoration offsets landing outside
    the originating chunk are silently skipped (`WorldGenerator::generate`,
    `src/worldgen/generator.cpp`) — no structure can straddle a chunk
    boundary yet, unlike the spec's stage-6 framing ("runs once neighbors
    are generated so trees/structures may cross chunk borders").
- Voronoi biome-cell resolution result caching (`vb/worldgen/
  biome_selector.hpp`'s `BiomeSelector::resolve`, Phase 6.14): deliberately
  recomputes its bounded neighbor-adjacency recursion from scratch on every
  call instead of memoizing across calls, trading cache-hit-rate for zero
  shared mutable state across `WorldGenWorkerPool` worker threads (no lock
  needed). If profiling ever shows this matters (repeated nearby-column
  queries within the same cell redo the same cheap recursion every time),
  a per-pipeline, mutex- or shard-guarded cache is the natural follow-up —
  not attempted here since the recursion is "only a handful of neighbors"
  per the design note and no perf problem has actually been observed.
- CSS-like declarative layout for `vb::script::UiRuntime` widgets
  (user-suggested, 2026-09-18): today every widget is placed with absolute
  pixel `x`/`y`/`w`/`h` (`content/base/ui/*.lua`, `docs/lua-api.md`) — a pack
  author does all positioning/responsiveness math by hand, including reading
  `client.screen_size()` (Phase 6.16) themselves to center anything. A
  flexbox/grid-flavored layout model (parent/child nesting, percentage or
  `flex`-style sizing, anchors) would let Lua describe *intent* ("centered",
  "fill remaining space", "bottom-right corner") instead of arithmetic,
  and would resize correctly with the window without every screen
  reimplementing that math. Explicitly post-first-playable — the current
  absolute-position model is sufficient for the existing modal screens/HUD;
  this is a bigger `UiRuntime`/`UiRenderer` redesign (a layout pass computing
  final `x`/`y`/`w`/`h` before widgets reach the renderer, most likely) worth
  doing once there's enough real UI content to justify it, not before.
