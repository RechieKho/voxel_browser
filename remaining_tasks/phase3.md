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

