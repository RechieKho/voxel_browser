## Phase 1 — Core Foundation & Networking

> Full history for this phase; linked from `REMAINING_TASKS.md`. Ground truth for [x] items — do not duplicate here.

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

- [x] **(spike)** librg v7.4.0 API investigated — findings + the §18 Q3 decision
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

