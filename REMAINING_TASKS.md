# Voxel Browser — Remaining Tasks

> Companion to `ARCHITECTURE_SPEC.md`. This is the implementation backlog to get
> from the current state to the "Minimum Playable Base" described in `README.md`.
>
> **This file is the lean core.** Every phase's full history (the detailed
> `[x]` write-ups — what shipped, why, which files, which tests) lives in its
> own `remaining_tasks/phaseN.md`, linked from each phase section below. Read
> this file for current status and what's actually left; open the linked file
> only when you need the historical detail behind a specific `[x]` line.

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

## Phase 0 — Project Restructure & Build System ✅ done (2026-09-10)

Prerequisite for everything else, not in the README's phase list. Full split
target/build system: `vb_core`/`vb_render`/client/server/tests, pinned
`FetchContent` deps gated behind `VB_WITH_*`, warnings/sanitizer cmake
modules, `VB_HEADLESS`, CI workflows, project renamed to `voxel_browser`.
Full detail: `remaining_tasks/phase0.md`.

**Remaining:**
- [x] First `git tag v0.0.1` so `git describe` yields a real version —
      landed and pushed to `origin` 2026-09-28 (`git describe` now returns
      `v0.0.1` instead of erroring with "No names found"). First real tag
      on the repo, so this is also the first time CI's `bundle`/`publish`
      steps will actually run against a real `git describe` version —
      worth checking that run once it appears.
- [x] doctest bumped `v2.4.11` -> `v2.5.3` (2026-09-30) — landed, but the
      `CMAKE_POLICY_VERSION_MINIMUM=3.5` shim stays: lz4 `v1.9.4`
      (`VB_WITH_COMPRESSION`) independently needs it too (`cmake_minimum_required
      (VERSION 2.8.12)`), confirmed by actually removing the shim and watching
      lz4's subbuild fail to configure under CMake >= 4. See
      `remaining_tasks/phase0.md` for the full note.
- [ ] Explicit source lists instead of relying on re-running CMake (already
      explicit; keep it that way as modules grow).

---

## Phase 1 — Core Foundation & Networking ✅ met (2026-09-11)

Goal: reliable client↔server handshake; two clients "see" each other in
network space; client window + render loop alive. Core primitives, transport
(`LoopbackTransport` + real-UDP `GnsTransport`), handshake FSMs, hand-rolled
`InterestGrid` replication (librg wired in behind `VB_WITH_REPLICATION`),
client shell (raylib window, `FirstPersonController`). Both binaries wired up
end-to-end; verified with two live processes over real UDP.
Full detail: `remaining_tasks/phase1.md`.

**Remaining:**
- [x] Surface `ENGINE_PROTOCOL_VERSION` mismatch in the client connect UI —
      already fully wired by the time this was checked (2026-09-28), no code
      change needed: `ClientHandshake::on_frame` (`src/net/handshake.cpp`)
      fails with `"engine protocol version mismatch"` both when the client's
      own check trips (`info->engine_protocol_version != kEngineProtocolVersion`)
      and when a server-side rejection frame (`kProtocolMismatch`) arrives;
      `src/client/main.cpp`'s `kConnecting` case (`client->failed() ?
      client->failure_reason() : ...`) already routes that string into
      `error_message` and `AppState::kError`, which `MainMenu::draw_error()`
      (`src/render/main_menu.cpp`) renders as a real raygui label with a
      "Back to menu" button — the Phase 5.3 main-menu error path this item
      said it was waiting on has existed since that phase landed. Covered by
      the existing `net_test.cpp` case "client rejects a protocol version
      mismatch". This item's own text was stale, tracking a gap that closed
      as a side effect of unrelated work rather than being picked up as its
      own task.
- [x] macOS CI now builds `VB_WITH_NET` — landed 2026-09-30 as a new
      `build_net_deps` job in `build_macos.yml`: builds protobuf v21.12 and
      OpenSSL 3.3.2 once per arch (arm64 native, x86_64 cross), `lipo`-merges
      the resulting static libs into one universal install prefix, uploads
      it as an artifact the `build` matrix downloads and feeds to
      `-DCMAKE_PREFIX_PATH`/`-DOPENSSL_ROOT_DIR`. **Not yet verified by a
      real GitHub Actions run** — this agent environment has no macOS
      runner; see `STATE.md`'s "Current status" for the full reasoning and
      what to check if the first real run fails.
- [x] The two-client replication test runs over `LoopbackTransport` only;
      re-run over `GnsTransport` — see "Current status" in `STATE.md` for
      the full writeup (landed 2026-09-28).

---

## Phase 2 — World State & Terrain Generation ✅ met (2026-09-11)

Goal: server generates terrain, streams chunks, client meshes and renders
them. `PalettedChunkStore`, `World`, fBm heightmap worldgen (determinism gate
green on all 3 platforms), per-chunk `LightEngine` + cross-chunk vertical
sky occlusion, chunk replication + `ClientChunkStore`, hand-rolled
face-culled mesher with a background mesh worker pool (Cellulose/greedy-merge
was tried and reverted — see `ARCHITECTURE_SPEC.md` §18 Q2 — hand-rolled
meshing is permanent).
Full detail: `remaining_tasks/phase2.md`.

**Remaining:**
- [x] Horizontal cross-chunk light propagation — landed 2026-09-28, closing
      this phase's last remaining item. `LightEngine::Neighbours`
      (`inc/vb/world/lighting.hpp`) extends `relight_chunk`/`relight_column`
      from vertical-only to all 4 horizontal neighbours, plus a `push`
      parameter that reactively relights an already-stable neighbour when a
      live edit opens a gap near a border (bounded to one hop by light's
      0-15 range vs. 32-block chunk width). Block light still doesn't cross
      chunk borders (only sky light does) — deliberately out of scope.
- [x] Frustum culling + transparent second pass — landed 2026-09-27. New
      header-only `inc/vb/render/frustum.hpp` (`build_frustum`/
      `aabb_in_frustum`) culls chunks provably outside the camera's 6-plane
      frustum before any draw call; `ChunkRenderer` now uploads separate
      opaque/transparent `GpuMesh` slots per chunk (`split_transparent()`)
      and draws transparent geometry in a back-to-front sorted second pass
      with depth-mask disabled.

---

## Phase 3 — Entity Component System & Physics ✅ substantially met (2026-09-11)

Goal: server-simulated players with voxel collision, movement synced to
clients with prediction/interpolation. EnTT registry wiring on the server
(2026-09-17), fixed 20 Hz tick loop (including `--singleplayer`'s integrated
server), swept-AABB voxel physics (`vb/physics/movement.hpp`), input
pipeline + prediction/reconciliation, remote-entity interpolation, and
billboard-sprite presentation (direction buckets, anim priority) for remote
entities — mechanism shipped and tested, real multi-window art not yet
eyeballed live.
Full detail: `remaining_tasks/phase3.md`.

**Remaining:**
- [x] System runner with explicit ordering (§7.2) — `vb::ecs::SystemRunner`
      (2026-09-25); script entities (Phase 6.1) are now real registry
      entities, giving it a genuine second consumer besides players. See
      `remaining_tasks/phase3.md`.
- [x] Client-side lightweight registry (2026-09-25) — `ClientSession`'s
      remote-entity interpolation bookkeeping now lives in a real
      `entt::registry` (`ecs::InterpBuffer`/`ecs::EntityKind`), not the old
      ad hoc `RemoteSample` struct. See `remaining_tasks/phase3.md`.
- [x] Per-player rate limit / flood guard — landed 2026-09-27 as
      `ServerSession::set_max_messages_per_second(double)` /
      `ServerConfig::max_messages_per_second` (`server.toml`, default 0 =
      unlimited). Token bucket per playing connection, one token per
      post-join message of any type, checked in `system_network_io()`;
      also closes Phase 6.3's custom-keybind-flood item (keybind bits ride
      the same `InputCmd`, same bucket). Exposed read-only via
      `vb.config.get("max_messages_per_second")`.
- [x] Step-up jerk: physics is exact but visually abrupt — landed 2026-09-28.
      New `vb::render::EyeHeightSmoother` (`inc/vb/render/camera.hpp`)
      exponentially eases the *rendered* eye Y toward the true feet+eye
      target over 0.12s instead of snapping — collision/physics untouched,
      only the drawn camera Y is smoothed; a jump past 2.0m (teleport/
      respawn) still snaps immediately.
- [x] Wall-clock `server_time_est` + smoothing on the client — landed
      2026-09-28. New `Transport::round_trip_time_seconds(ConnId)`
      (`GnsTransport` overrides it with real ping) feeds
      `vb::net::ServerTimeEstimator` (`inc/vb/net/server_time_estimator.hpp`);
      `interpolated_pos()` now targets this continuously-advancing estimate
      instead of the last received tick, fixing a real pre-existing bug
      where interpolation froze solid between snapshot arrivals.
- [x] `SpriteVisual`-equivalent client state (atlas handle, `facings`,
      per-clip frame lists) for entity kinds — landed 2026-09-25 as Phase
      4's `visual = {...}` item below (`EntityRenderer`'s `KindVisual`/
      `EntityVisualLayout`, entity-management follow-up).
- [x] Real billboard art (atlas, per-clip frames) — `base:player`/
      `base:dropped_item` now ship real checked-in spritesheets, see Phase
      4's own "Real base-pack art" entry, landed 2026-09-25.

---

## Phase 4 — The "Browser" Engine (Scripting & Assets) ✅ met — mechanism, not content

Goal: server logic + content defined in Lua; client auto-downloads pack
assets; Lua-defined UI. Sandboxed Lua 5.4 + sol2 VM, `PackRuntime`
registration/event-bus/scheduling API, `S2C_BlockRegistry` replication, full
Asset Sync protocol (manifest, chunked transfer, content-addressed client
cache, virtual pack FS), and a separate client-side `UiRuntime` + raygui
renderer driven by `player:open_ui`. All five subsystems (4.1-4.5) work
end-to-end over `LoopbackTransport`; what was missing was content, closed by
Phase 5.1.
Full detail: `remaining_tasks/phase4.md`.

**Remaining:**
- [x] Custom `require` over the pack's own virtual module filesystem +
      per-callback wall-clock budget — landed 2026-09-27. In-memory
      `path -> source text` map built from a recursive walk of the pack's
      `.lua` files (except `ui/*.lua`), installed via `Vm::install_require()`;
      cycle detection, `package.loaded`-style caching. `VmLimits` gains a
      250ms `wall_clock_budget_ms` checked from the existing instruction hook
      (`kHookPeriod` 1000), classified to the existing `kBudgetExceeded`.
- [x] `EntityKind` tick/spawn/hit/death callbacks wired into real systems —
      landed 2026-09-27, now that `SystemRunner` (Phase 3.1) exists.
      `ServerSession::set_script_tick_hook` is a new `"script_tick"`
      `SystemRunner` phase, so a script-driven entity move now reaches that
      same tick's `sync_interest`/`broadcast_snapshots` instead of one tick
      late.
- [x] `register_entity`'s `visual = {...}` sub-table (variant/facings/clips)
      for 3.5's `entity_renderer` — landed 2026-09-25 (protocol **19 -> 20**).
      Client-side `entity_visual_layout.hpp` computes per-clip layout,
      validated against the real decoded PNG; a mismatch falls back to the
      flat placeholder rather than failing pack load.
- [x] Per-instance `ScriptState.visual_override` (skins) — landed 2026-09-25
      (protocol **20 -> 21**). `vb.world.spawn(kind, pos, {visual_override=})`
      merges independently-optional fields over the kind's own `visual`;
      fixed at spawn time only — no live-update/clear path yet.
- [x] Real base-pack art for `base:player`/`base:dropped_item` — landed
      2026-09-25 via a new `vb.register_entity{represents="player"|
      "item_drop"}` field. **Follow-up (2026-09-27):** front/side/back poses
      were too similar to tell apart; `base:player` swapped to debug-styled
      F/R/B/L art plus a new `visual = {mirror = false}` option for a
      distinct pose per facing (protocol 22 -> 23) — also fixed a real
      pre-existing bug where a facings=4 kind was picking its pose with
      hardcoded facings=8 sector math.
- [x] Real texture/atlas system landed 2026-09-23: `vb.register_block{
      texture=...}` -> `S2C_BlockRegistry` -> per-session `TextureAtlas` ->
      real per-face UVs. Proved on `base:stone`/`base:water` only — a full
      base-pack reskin (dirt/grass/sand/wood/leaves) is a separate follow-up.
- [x] Manifest staleness: a pack writing `vb.storage` *after* startup used
      to go stale for the server process's life — landed 2026-09-28. New
      `PackRuntime::storage_revision()` counter, polled once/sec by
      `src/server/main.cpp` to rebuild + atomically swap the asset manifest
      via a new `ManifestHolder`. `--singleplayer` is out of scope (never
      builds a manifest).
- [x] Item grid widget for `UiRuntime` — landed 2026-09-28. A new generic
      `icon` `WidgetType` draws one registered block/item id's real atlas
      texture; `content/base/ui/inventory.lua` composes a real item grid out
      of it plus `rect`/`text`.
- [ ] `--singleplayer`'s registry-wiring gap is closed (Phase 5.1); no
      remaining item here.

---

## Phase 5 — Minimum Playable Base ✅ met over `LoopbackTransport`

Goal: a small, coherent, playable multiplayer sandbox. `content/base` (blocks,
biomes, crafting, dropped items, real inventory, hotbar), block break/place
over the network with a Lua veto seam and hold-to-break timing, a full raygui
main menu / connect / settings flow, and play polish (day/night, chat, player
list, death/respawn). Verified via automated tests + `--headless` runs; the
literal "two real windows, playing together" manual pass across all 3
platforms has not been run by a human yet.
Full detail: `remaining_tasks/phase5.md`.

**Remaining:**
- [x] Player + dropped-item billboard sprite atlases (§11.3/3.5) — landed
      2026-09-25, see Phase 4's "Real base-pack art" entry.
- [x] Cross-chunk relight on edit (breaking a floor lets light into the chunk
      below) — confirmed already closed 2026-09-28, no code change needed;
      `WorldReplicator::apply_block_edit()` has called the cross-chunk
      `relight_column` cascade (Phase 2) on every real edit since that
      pass landed. New end-to-end regression test proves it, not just
      re-reading the code. `relight_chunk()` still recomputes from scratch
      each time (deliberate perf characteristic, not a gap).
- [x] Per-block hardness/tool break-time variation — landed 2026-09-28.
      `ServerSession::punch()`/`player:punch()` gained an optional
      `block_damage` parameter (default 1); per-block hardness already
      existed (`BlockType::max_damage`, 6.5) — this is the matching "tool"
      half, a pack-side decision with no tool concept in the engine itself.
- [x] Keybindings screen (5.3 Settings) — a new Settings -> Keybindings
      raygui screen (`MainMenu::draw_keybindings`) lets a player rebind any
      of the 6 `MovementBindings` axes to a physical key, persisted to
      `client.toml`, applied live. Distinct from Phase 6.19's
      `vb.register_keybind` name registry — the two compose.
- [x] Connect-screen byte-progress bar — landed 2026-09-28 (was status-text
      only). `ClientAssetCache::sync_total_bytes()`/`sync_received_bytes()`
      derive real progress from the existing transfer map;
      `MainMenu::draw_connecting()` draws a real `GuiProgressBar` once a
      fraction is known. Phase 7.1's loading-screen entry named this as a
      likely shared prerequisite — now closed.
- [ ] Live two-window manual playtest (chat + crafting + seeing each other,
      all at once, across all 3 platforms) — not yet run.

---

## Phase 6 — Lua-Driven Extensibility ✅ 6.1–6.16, 6.18, 6.20–6.21 done; 6.17 superseded

Goal: content packs override/extend engine defaults the way block
registration already does — biomes, entities, UI, input, data. Landed:
per-instance Lua entity objects (6.1), reactive `render(state)` UI (6.2),
custom keybinds (6.3), `vb.db`/`vb.crypto` (6.4), block-damage system (6.5),
player damage/death primitive (6.6), physics/day-night/inventory/chat/
item-drop override surfaces (6.7-6.11), read-only config visibility (6.13),
Lua-driven FastNoise2 worldgen pipeline + Voronoi biome selection (6.14), a
`content/examples/kitchen_sink` pack exercising all of the above (6.15), an
engine-neutral client HUD primitive (`ui.define_hud` + `kRect`, 6.16),
Growtopia-style discrete punch combat with block self-heal (6.18 — this is
the current shape of block breaking, **not** 6.17's original hold-to-break
plan, which was superseded same-day and is kept only for historical record),
right-click placing decoupled into content the same way (6.20 — closes
6.17/6.18's own "Placing... unaffected" gap; `player:place_block()` is now
the validated primitive, `content/base/mechanics.lua` decides when/what), and
a unified, pack-overridable interaction reach + physics/action read-back
surface (6.21 — `vb.action.set_params{reach=}`/`get_params()` and
`vb.physics.get_params()`).
Full detail: `remaining_tasks/phase6.md`.

**Remaining:**
- [x] **6.22: fall damage** — landed 2026-09-27 as `net::ServerSession::
      set_landed_hook(fn(NetId, impact_speed))`, firing once per player on
      the tick a fall is arrested. `vb.on("player_landed", ...)` is the new
      Lua event (opt-in, mechanism not policy); `content/base/
      fall_damage.lua` is the reference policy (8 m/s safe threshold, 1 HP
      per m/s above it).
- [x] **PvP confirmed already real, not stale text** — checked 2026-09-28.
      `ServerSession::punch()` already resolved player-vs-player hits and
      `content/base/mechanics.lua` already dispatched them; what was
      missing was proof, closed by a new `netcode_test.cpp` case driving 20
      punches to a confirmed kill+respawn with `cause == "pvp"`.
- [x] **Hunger primitive** — landed 2026-09-28. New `vb::ecs::Hunger`
      component + `ServerSession::HungerParams` (decay/starvation rates,
      both `0.0` by default), a new `"update_hunger"` `SystemRunner` phase,
      and `vb.hunger.set_params{}`/`player:get_hunger()`/`add_hunger()`.
      Server-side bookkeeping only, no client HUD/replication. Also fixed a
      real pre-existing link-error gap: `effective_action_params()` (6.21)
      had no `!VB_WITH_LUA` stub at all.
- [x] **Mob damage** — landed 2026-09-28 as `content/base/entities/
      zombie.lua`, a real hostile mob using existing damage primitives —
      no new engine mechanism needed. **Found and worked around, not fixed,
      a real engine bug while building this:** a `script::PlayerHandle`
      stored across calls and read back from inside `vb.on("tick", ...)` or
      an entity's own `on_tick` silently returns corrupted data (confirmed
      with ASan) — see Cross-Cutting's own entry below for the full repro.
      `zombie.lua` works around it by driving chase/attack from
      `vb.on("player_input", ...)` instead, which always hands over a fresh
      handle.
- [x] Automatic despawn-on-health trigger for generic script entities —
      landed 2026-09-25. Opt-in via `vb.register_entity{health=...}`;
      `entity:damage()` now decrements real HP and auto-despawns at 0
      instead of requiring the pack to track it and call `:remove()`.
- [x] Client-side kind-specific rendering for script entities — landed
      2026-09-25 (`EntityKind` id drives billboard width/height via
      `S2C_EntityKindRegistry`, protocol 19). Real per-kind sprite art
      landed the same day, see Phase 4's own entry.
- [x] Per-connection rate limiting on custom-keybind events — closed
      2026-09-27 as part of Phase 3's flood guard (same token bucket
      already covers keybind bits riding inside `InputCmd`).
- [x] Replicate block-damage *value* to nearby players — landed 2026-09-27.
      `S2C_BlockDamage` (protocol 25) fans out every live punch-count
      change; `client.break_progress()` (6.16) now reports a real fraction
      instead of always `nullopt`, with a default darkening crack overlay.
- [x] Real crack-stage texture art + `crack_texture` override — landed
      2026-09-27, closing 6.5 in full (protocol **25 -> 26**). New
      `vb::render::CrackAtlas`/`CrackOverlay` replace the flat overlay with
      real per-stage texture art. Also fixed a real pre-existing bug: three
      `BlockRegistryRecord` call sites were silently dropping `max_damage`
      on every hop, so `break_progress()` was permanently `nullopt`.
- [x] Movement/action key bindings extended into the 6.3 keybind registry
      (6.19) — movement/primary/secondary are pre-registered by every
      `PackRuntime`, so a pack reads them like any custom keybind.
      Physical-key rebinding is Phase 5.3's separate keybindings screen.
- [x] HUD widgets wired to `report_click`/`report_change`/`report_list_change`
      — landed 2026-09-27.
- [x] Player list / chat log / hotbar migrated off hardcoded C++ into
      `ui.define_hud` — landed 2026-09-27. New `WidgetType::kText` (colored,
      alignable) plus 6 new `client.*` read-only accessors; the chat input
      box itself deliberately stays a plain `GuiTextBox`, never a widget.
- [x] No punch-rate cooldown enforced engine-side — landed 2026-09-28 as
      `PunchParams::punch_cooldown_seconds` (default `0.0`, opt-in). A whiff
      arms the next cooldown just like a landed hit. Still open: no swing
      animation, no PvP armor/knockback.
- [x] Held item / hotbar selection — landed 2026-09-25, closing 6.20's own
      gap. Protocol **21 -> 22**: `InputCmd` gains `u8 selected_slot`.
      `player:get_selected_slot()`/`get_held_item()` let placing read what's
      actually in hand instead of an infinite hardcoded stone dispenser.
- [x] **6.21:** block-edit reach and punch reach unified into one
      pack-overridable `net::ActionParams::reach`, replacing
      `WorldReplicator`'s old hardcoded constant. New
      `vb.action.set_params{reach=}`/`get_params()` and
      `vb.physics.get_params()` (its own namespace, not `vb.combat`).

---

## Phase 7 — World & UX Polish (7.1-7.6 done)

> User-requested (2026-09-19): four UX gaps, scoped as their own phase since
> none of them extend Phase 6's "default + override" system pattern the way
> 6.1-6.20 did — these are new engine surfaces (loading UI, fog, liquid
> collision/vision/regions) plus closing a real content gap found while
> reviewing Phase 6 (`ui/pause.lua`/`ui/inventory.lua`'s own "nothing opens
> this yet" comments).
Full detail: `remaining_tasks/phase7.md`.

- [x] **7.1 — Engine-side loading screen with progress.** Landed 2026-09-22:
      a new `AppState::kLoading`, entered right after a successful join and
      left once the initial view-box of chunks has streamed in (or an
      8-second deadline elapses). Stage 1 is a generic progress bar with no
      data dependency; stage 2 overlays operator-level branding (motd text
      only, no color knob) once `server.toml` config arrives. Not
      Lua-driven — this covers the window before any pack content is even
      guaranteed loaded.
- [x] **7.2 — Distance fog, adjustable from Lua.** Landed 2026-09-22: a real
      GLSL fog shader in `ChunkRenderer`, fog color always derived from the
      current sky color (never independently Lua-settable), only start/end
      distance is pack-overridable (`vb.render.set_fog{start=,end=}`,
      protocol 16, `S2CFogParams`).
- [x] **7.3 — Walkable liquid blocks: collision, underwater rendering,
      generic region hook.** Landed 2026-09-23. Collision needed no change.
      Underwater uses the same fog mechanism at a tighter distance. Two
      same-day follow-up fixes: a pre-existing face-culling bug made
      submerged terrain invisible (fixed by culling liquid neighbours only
      against liquid current voxels), and water's alpha was bumped back to
      opaque once that terrain started actually rendering. New
      `BlockType::region` flag + `ServerSession::update_region_occupancy()`
      fires `vb.on("region_enter"/"region_exit", ...)` on a single-point
      crossing test — no flowing-liquid physics, out of scope per user
      instruction.
- [x] **7.4 — Wire `content/base`'s UI screens to a real trigger.** Landed
      2026-09-23. Root cause was one level deeper than a keybind: nothing
      mapped a *physical key* to a pack-registered custom keybind name. New
      `kCustomKeybinds` hardcoded default table (Escape/E) plus
      `content/base/keybinds.lua` opening pause/inventory on the rising edge.
- [x] **7.5 — Underwater fog tint defaults to the liquid block's own color,
      overridable from Lua.** Supersedes 7.2/7.3's "fog color never
      independently settable" decision for the underwater case only.
      Default (2026-09-23): real average pixel color of the liquid's synced
      texture. Override (2026-09-27): `vb.render.set_fog{underwater_tint=
      {r=,g=,b=}}`, protocol **23 -> 24**.
- [x] **7.6 — World persistence: chunks survive a server restart.** Landed
      2026-09-25, favoring flat per-region files over LMDB (reversing an
      earlier direction note). New `vb::world::RegionStore` groups chunks
      into one file per 16x16-chunk X/Z region; only *edited* chunks are
      ever persisted (`Chunk::revision() == 0` means "regenerate is
      equivalent"). Three new `server.toml` keys
      (`persist_world`/`world_dir`/`autosave_interval_seconds`).
      LZ4/zstd region-file framing and `--singleplayer`'s own `RegionStore`
      wiring were both deliberately deferred, then closed 2026-09-28 (see
      Cross-Cutting).

---

## Phase 8 — Developer CLI (`vb`) — planned (2026-10-04)

User-requested: a `vb` command-line tool that downloads and manages installed
copies of Voxel Browser in the **user's profile** (no admin rights), and
makes hosting a server a one-command affair (`vb host`, plus named
background instances via `vb server …`). Full design, layout, command
surface and per-phase task lists: `architecture_spec/dev-cli.md` §11.

- [x] **8.1 — Release pipeline produces installable artifacts** (prerequisite):
      per-platform `voxel_browser-<ver>-<os>-<arch>.zip` assets + a
      `release.toml` with SHA-256s; today's `bundle.yml` flat-merges every
      platform's identically-named files into one artifact.
- [x] **8.2 — `vb` skeleton + local version management**: `user_data_dir()`/
      `user_config_dir()`/`VB_HOME`, `vb list/use/which/uninstall/link/launch`.
- [ ] **8.3 — Download & install**: curl + miniz behind `VB_BUILD_CLI`,
      verified atomic install transaction, `vb install/update/prune/doctor`.
- [ ] **8.4 — Hosting**: engine `--stop-file` (Windows graceful stop) and
      client `--content-pack`/`--world-dir` for `--singleplayer`;
      `vb host`, `vb server new/start/stop/status/logs/rm`.
- [ ] **8.5 — Polish**: `list --remote`, `--json`, `self update`, bootstrap
      scripts, server `--status-file`, completions, `vb host --watch`.
- [ ] **8.6 — Hardening**: signed manifests, service-unit printing,
      protocol-mismatch warnings, arm64 artifacts.

---

## Cross-Cutting / Continuous

Full detail: `remaining_tasks/cross_cutting.md`.

- [x] **`script::PlayerHandle` stashed across ticks returning/crashing on
      corrupted data** (found 2026-09-28 building
      `content/base/entities/zombie.lua`, root-caused and fixed 2026-09-30).
      Real cause: sol2 pushes a non-const lvalue reference to a registered
      usertype as a raw pointer into the caller's own C++ stack frame, not a
      copy, unless `SOL_FUNCTION_CALL_VALUE_SEMANTICS` is defined on — every
      `PlayerHandle` dispatch call site constructs a named local and passes
      it straight into the Lua call, so a pack script that stores that
      argument beyond the call (a global, table field, upvalue — storage
      location never mattered) holds a dangling stack pointer the instant a
      *different* C++ call path reuses that address. Fixed by defining
      `SOL_FUNCTION_CALL_VALUE_SEMANTICS=1` on the `sol2` target
      (`cmake/Dependencies.cmake`) — `PlayerHandle` is the only usertype this
      project registers and is a stateless proxy, so forcing copy semantics
      is free. Full root-cause writeup, the empirical pointer-identity proof,
      and verification detail in `remaining_tasks/cross_cutting.md`.
- [ ] Keep `ENGINE_PROTOCOL_VERSION` + `docs/protocol.md` in lockstep with every
      wire change.
- [ ] Every new `vb/protocol` struct gets a round-trip + fuzz test.
- [x] Sanitizer (ASan/UBSan) debug CI job; TSan job for the threaded
      subsystems — landed 2026-09-28 on the Linux matrix only (MSVC has no
      UBSan/TSan support; macOS CI doesn't build `VB_WITH_NET` yet). Not
      verified by an actual GitHub Actions run — this agent environment
      can't trigger one.
- [ ] Determinism golden-value CI gate stays green across platforms.
- [x] Soak test target (N simulated clients, random walk + edits) — landed
      2026-09-28 as `tests/unit/soak_test.cpp`, folded into the normal
      `vb_tests` run. 4 simulated clients over `LoopbackNetwork`
      random-walk + break/place-edit for 150 ticks; asserts connection/edit-
      queue/loaded-chunk counts all stay bounded rather than growing, and
      that everything cleans up to 0 after disconnect. Deliberately kept
      small (real fBm terrain generation dominates cost far more than the
      sim logic being soaked).
- [x] Perf budget checks: chunk mesh time, snapshot size — landed 2026-09-28
      as `tests/unit/perf_budget_test.cpp`. Chunk mesh time asserts
      `< 100ms` (measured ~21ms); snapshot size asserts a per-entity byte
      budget (measured ~52 bytes/entity vs. a 90-byte budget). Frame time
      deliberately left out — needs a live GL context this environment
      doesn't have.
- [ ] `--headless` stays functional for both binaries (CI + integration tests).
- [ ] Address the remaining open item(s) in `ARCHITECTURE_SPEC.md` §18 as
      their blocking phase arrives (renumbered from §19; every row is now
      resolved or has a noted direction — Q4 chunk compression resolved
      2026-09-28 (LZ4 wired generically into `frame_message()`, see that
      row); Q5 persistence has region-file LZ4 framing resolved 2026-09-28
      too (`RegionStore` format version 1 -> 2, see that row), and
      `--singleplayer`'s `RegionStore` wiring is also now resolved
      (2026-09-28, see "Current status" in `STATE.md`) — Q5 has no open sub-
      item left; Q6 auth has a direction set but isn't implemented yet);
      record decisions in that section.

---

## Deferred (post first-playable)

region file format versioning/migration · token auth verification · audio subsystem + Lua sfx/music API · server-side
plugin hot-reload · entity-entity physics/mounts/projectiles · particle
system beyond block-break puffs · compression tuning (zstd, snapshot deltas,
bit-packed inputs) · dedicated server browser/master list · modding
(stacked packs, dependency resolution) · rule-based decorative structure
placement for worldgen (depends on 6.14, which landed the prerequisite —
this item itself not attempted) · Voronoi biome-cell resolution caching
(6.14, no perf problem observed yet) · CSS-like declarative layout for
`UiRuntime` widgets (today: absolute pixel positioning only).

Full reasoning for each: `remaining_tasks/deferred.md`.
