## Phase 3 — Entity Component System & Physics

> Full history for this phase; linked from `REMAINING_TASKS.md`. Ground truth for [x] items — do not duplicate here.

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
- [x] System runner with explicit ordering (§7.2), 2026-09-25:
      `vb::ecs::SystemRunner` (`inc/vb/ecs/system_runner.hpp`,
      `src/ecs/system_runner.cpp`) — a named, ordered list of
      `void(entt::registry&, const TickContext&)` steps, run in registration
      order. `ServerSession::build_systems()` registers `tick()`'s phases
      under it (`network_io`, `handshake_timeouts`, `advance_time_of_day`,
      `check_respawns`, `update_item_drops`, `update_block_damage`,
      `update_block_punch_healing`, `update_region_occupancy`,
      `sync_interest`, `advance_server_tick`, `broadcast_snapshots`,
      `broadcast_world`) — same behavior, but now a single greppable,
      inspectable table instead of an implicit call sequence. Gets a real
      second registry consumer at the same time: `spawn_script_entity`/
      `set_script_entity_state`/`remove_script_entity` (Phase 6.1's script
      entities) now create/update/destroy a real registry entity
      (`Position`/`EntityKind`/`NetReplicated`, +`Rotation`/`Velocity` once
      moved) instead of only touching the interest grid, and the new
      `system_sync_interest()` generically pushes every such entity
      (`registry_.view<Position, NetReplicated>(exclude<PlayerTag>)`) into
      `interest_` once a tick, replacing what used to be per-mutator manual
      upserts. Players deliberately stay on their existing *immediate*
      upsert path (`handle_input_batch`, `check_respawns`, `set_player_state`,
      join) rather than folding into the same generic pass — several other
      network_io-phase handlers this same tick (`handle_block_edit`,
      `handle_block_break_begin`, `punch()`) need this tick's
      just-simulated position, not last tick's, so they now read
      `ecs::Position`/`ecs::Rotation` straight off the registry instead of
      `interest_` where that freshness matters. `PackRuntime::dispatch_tick`
      stays externally driven (unchanged) — see `ARCHITECTURE_SPEC.md`'s
      note for why folding `ScriptPreTickSystem`/`ScriptPostTickSystem` into
      this runner was scoped out.
- [x] Client-side lightweight registry, 2026-09-25: `ClientSession` gained
      `entity_registry_` (`entt::registry`) + `net_to_entity_`
      (`NetId -> entt::entity`); the bespoke `RemoteSample` struct and
      `remote_samples_` map are gone, replaced by the already-defined-but-
      previously-unused `ecs::InterpBuffer` component
      (`inc/vb/ecs/components.hpp`) as the real interpolation bookkeeping,
      plus `ecs::EntityKind` per remote entity. `remote_` (flat "latest
      `EntityRecord` per net id" map) and the public `remote_entities()`/
      `interpolated_pos()` API are unchanged, so `entity_renderer.cpp` and
      the 3 test files that read them needed no changes.

### 3.2 Input pipeline

- [x] `InputCmd { seq, dt, move, yaw, pitch, buttons }` + `C2S_InputBatch`
      (lane 4) — `inc/vb/protocol/input.hpp`, round-trip tested. Client keeps an
      unacked history ring (`ClientSession::push_input`) and resends it each frame.
- [x] Input ingest: `ServerSession::handle_input_batch` — skips already-simulated
      `seq`, clamps `dt` to [0, 0.1], caps the batch at 64 cmds on decode.
- [x] Per-player rate limit / flood guard — landed 2026-09-27 as
      `ServerSession::set_max_messages_per_second(double)` /
      `ServerConfig::max_messages_per_second` (`server.toml`, default 0 =
      unlimited). A token bucket per playing connection (one token per
      post-join message, any type -- input batch, block edit, chat, UI
      event, block-break begin/stop -- refilled at the configured rate,
      capacity == one second's worth of burst), checked in
      `system_network_io()` before a message is dispatched to its handler;
      an empty bucket drops the message (not the connection). Defense in
      depth on top of the existing closed-schema per-message caps
      (`C2SInputBatch::kMaxCmds`, the fixed-width keybind bitset) -- those
      bound how much one message can do, this bounds how often a connection
      can send one at all. Exposed read-only via `vb.config.get(
      "max_messages_per_second")` (Phase 6.13's surface), same posture as
      `max_connections_per_ip`. Also closes Phase 6.3's custom-keybind-flood
      item: custom-keybind bits ride inside `InputCmd`/`C2SInputBatch` like
      every other input field, so this same token bucket covers a
      custom-keybind flood too — no keybind-specific limiter was needed on
      top of it. Verified: full `vb_tests` 380/380 green over
      `LoopbackTransport` (a new `netcode_test.cpp` case drives 3
      back-to-back chat sends through a 1 msg/sec limit, confirms only the
      first lands, then confirms a 4th lands again after ~1s of ticks refill
      the bucket; `config_test.cpp`/`pack_runtime_test.cpp` cases cover the
      TOML default/parse and `vb.config.get` round-trip) — 2 real-UDP
      `gns_transport_test.cpp` cases skipped in this run (this agent
      environment's Windows Firewall blocks unattended real-UDP
      listen/connect), clean `-Werror` build of `vb_tests`/`voxel_browser`/
      `voxel_browser_server`.

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
- [x] Step-up jerk: physics is exact but visually abrupt — landed 2026-09-28.
      New `vb::render::EyeHeightSmoother` (`inc/vb/render/camera.hpp`, pure
      math, unit-tested without a GL context, same posture as
      `frustum.hpp`/`entity_visual_layout.hpp`): each frame it exponentially
      eases the *rendered* eye Y toward the true `feet.y + eye_height` target
      over a short (0.12s) time constant, rather than setting the camera to
      it directly — a step-up's instantaneous 1-tick position jump (up to
      `step_height`, 1.05m) now reads as a quick smooth rise instead of a
      pop. A jump bigger than `kSnapThreshold` (2.0m — above any real
      step-up, below a teleport/respawn distance) is applied immediately with
      no smoothing, so a respawn or reconnect never eases in from the old
      body's position. Purely a render concern: `feet`/collision/physics are
      completely untouched, `X`/`Z` are still set directly from `feet` every
      frame (only `Y` gets smoothed) — the "physics is exact" half of the
      original gap is unchanged, only what's drawn changes.
      `src/client/main.cpp` wires one `EyeHeightSmoother` per client loop
      (both the `--headless` path and the real windowed one), reset alongside
      `controller` at spawn/join and in `enter_playing()` (reconnect/respawn)
      so a fresh life never inherits a stale in-flight smoothing state from a
      previous one. Verified: full `vb_tests` 388/388 green (6 new
      `render_test.cpp` cases: first-update snaps, a step-up eases in over
      one frame rather than landing immediately, converges to the target
      after enough time, a large jump snaps immediately, a tiny continuous
      delta tracks almost exactly, and `reset()` drops any in-flight
      smoothing), clean `-Werror` build of `vb_tests`/`voxel_browser`/
      `voxel_browser_server`. The actual smoothed step-up (a human walking
      up a single block and watching the camera rise instead of pop) was
      **not** manually eyeballed — no GUI in this agent environment.
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
- [x] Wall-clock `server_time_est` + smoothing on the client (needs
      `GnsTransport` RTT — loopback has no latency to estimate) — landed
      2026-09-28. New `Transport::round_trip_time_seconds(ConnId)` (default
      `nullopt`, `GnsTransport` overrides it with GameNetworkingSockets' own
      real-time ping) feeds a new header-only `vb::net::ServerTimeEstimator`
      (`inc/vb/net/server_time_estimator.hpp`, same pure-math/unit-tested-
      without-a-session posture as `render::EyeHeightSmoother`) that
      `ClientSession` advances every `tick()` and nudges on every snapshot.
      `interpolated_pos()` now targets this continuously-advancing estimate
      instead of the last *received* tick number, which used to freeze the
      interpolation fraction solid between snapshot arrivals (a real,
      previously-unnoticed bug this item's investigation surfaced, not
      just the originally-scoped "add RTT" gap). See `STATE.md`'s "Current
      status" for the full writeup.
- [x] Map players ↔ librg network entities — `InterestGrid::upsert`/`remove` track
      every `NetId` (players and item drops alike) 1:1 as a self-owned librg
      entity; see `src/replication/interest.cpp`.

### 3.5 Entity visual presentation — billboard sprites (§11.3)  ✅ (placeholder art)

**Gap closed:** nothing used to draw a remote player or entity at all —
`remote_entities()`/`interpolated_pos()` (3.4) gave correct positions, but the
client only rendered terrain. Design: `ARCHITECTURE_SPEC.md` §11.3 (Don't
Starve-style Y-axis-billboarded, directionally-animated sprites, decided
2026-09-11 — see §18 Q7).

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

