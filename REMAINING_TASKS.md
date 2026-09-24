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
- [ ] First `git tag v0.0.1` so `git describe` yields a real version and
      `bundle`/`publish` are exercised.
- [ ] `CMAKE_POLICY_VERSION_MINIMUM=3.5` shim is set for CMake ≥ 4 (doctest
      2.4.11 declares `cmake_minimum_required(3.0)`); drop it if doctest is bumped.
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
- [ ] Surface `ENGINE_PROTOCOL_VERSION` mismatch in the client connect UI
      (both FSMs already reject it; needs the Phase 5.3 main-menu error path).
- [ ] macOS CI doesn't build `VB_WITH_NET` (universal arm64+x86_64 build vs.
      single-arch brew protobuf) — needs a universal protobuf, see `build_macos.yml`.
- [ ] The two-client replication test runs over `LoopbackTransport` only;
      re-run over `GnsTransport`.

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
- [ ] Horizontal cross-chunk light propagation (sideways-only spill under a
      horizontal overhang spanning a chunk border) — still per-chunk-only.
- [ ] Frustum culling, transparent second pass, texture atlas — Phase 4.

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
- [ ] System runner with explicit ordering (§7.2) — only worth adding once a
      Lua entity kind needs to iterate the registry generically.
- [ ] Client-side lightweight registry (currently `ClientSession` holds
      predicted state inline).
- [ ] Per-player rate limit / flood guard belongs with `GnsTransport`.
- [ ] Step-up jerk: physics is exact but visually abrupt; needs a render-only
      eye-height smoothing layer client-side (not attempted).
- [ ] Wall-clock `server_time_est` + smoothing on the client (needs
      `GnsTransport` RTT — loopback has no latency to estimate).
- [ ] `SpriteVisual` component (atlas handle, `facings`, per-clip frame
      lists) for entity kinds — nothing produces one until 4.2.
- [ ] Real billboard art (atlas, per-clip frames) — pack-defined, waits on
      4.2's `visual = {...}` + Asset Sync + base-pack sprites (5.1).

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
- [ ] Custom `require` over the virtual pack FS + per-callback wall-clock
      budget — needs the synced asset FS (4.4), still deferred.
- [ ] `EntityKind` tick/spawn/hit/death callbacks wired into real systems —
      superseded in practice by Phase 6.1's hardcoded-system approach; the
      formal `SystemRunner` itself is still `[ ]` (see Phase 3.1).
- [ ] `register_entity`'s `visual = {...}` sub-table (variant/facings/clips)
      for 3.5's `entity_renderer` — schema finalized, not implemented; nothing
      reads a per-kind visual def yet.
- [x] Real texture/atlas system landed 2026-09-23 (see
      `state/changelog-recent.md`): `vb.register_block{texture=...}` ->
      `S2C_BlockRegistry` -> a per-session `vb::render::TextureAtlas` built
      from Asset Sync (or, `--singleplayer`, straight off disk) -> real
      per-face UVs in `ChunkRenderer`. Proved on `base:stone`/`base:water`
      only (user-scoped) — **remaining:** every other base block (dirt,
      grass, sand, wood, leaves) still renders its old flat placeholder
      color; a full base-pack reskin is a separate follow-up pass, not a
      mechanism gap.
- [ ] `--singleplayer`'s registry-wiring gap is closed (Phase 5.1); no
      remaining item here.
- [ ] Manifest staleness: a pack that writes `vb.storage` *after* startup
      (not just at load time) goes stale for the rest of that server
      process's life — no shipped pack triggers this today, left unaddressed.
- [ ] Item grid widget for `UiRuntime` — needs a real item/inventory concept.

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
- [ ] Player + dropped-item billboard sprite atlases (§11.3/3.5) — replaces
      the flat placeholder quad; not started.
- [~] Cross-chunk relight on edit (breaking a floor lets light into the chunk
      below) — still per-chunk from scratch each edit.
- [ ] Per-block hardness/tool break-time variation — one flat duration today
      (superseded in direction by Phase 6.17/6.18's punch-based combat, but
      the "vary by block/tool" idea itself is still open).
- [x] Keybindings screen (5.3 Settings) — a new Settings -> Keybindings
      raygui screen (`MainMenu::draw_keybindings`) lets a player click an
      action's key and press any physical key to rebind it, for the 6
      `MovementBindings` axes (forward/back/left/right/jump/sprint — mouse
      break/place buttons are left alone, matching the read-only
      `MOUSE_BUTTON_LEFT`/`RIGHT` convention elsewhere). Persisted as 6 new
      `key_*` int fields on `ClientConfig`/`client.toml`
      (`vb::core::save_client_config`/`load_client_config`), applied to
      `src/client/main.cpp`'s live `MovementBindings` immediately on Save —
      no restart needed, unlike window size/vsync. **Note:** this rebinds
      the *physical key*, distinct from Phase 6.19's `vb.register_keybind`
      name registry (which is about a pack reading `input.keybinds["jump"]`
      by name, not which key produces it) — the two compose: rebinding
      "Jump" here still shows up as the same `keybinds["jump"]` bit to Lua.
- [ ] No connect-screen byte-progress bar (status-text-only) — asset-sync
      never grew progress-fraction accounting.
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
- [ ] No fall damage, PvP, mob damage, or hunger — 6.6 only added the
      primitive (`player:damage`) and the decision hook (`player_death`); no
      content calls either yet.
- [ ] No automatic despawn-on-health trigger for generic script entities (no
      health primitive exists; a pack tracks HP on `self` itself) — 6.1.
- [ ] No client-side kind-specific rendering for script entities (`EntityKind`
      id threads through but nothing branches on it) — 6.1.
- [ ] Per-connection rate limiting on custom-keybind events — 6.3 (folds into
      Phase 3.2's still-unimplemented flood guard).
- [ ] Replicate block-damage *value* (not just begin/stop/complete) to nearby
      players — 6.5, prerequisite for a crack overlay.
- [ ] Default generic crack overlay + `crack_texture` override — 6.5. The
      real texture/atlas system this was blocked on landed 2026-09-23 (see
      Phase 4's own item), but this specific piece is still `[ ]`: no
      crack-overlay art exists, and 6.5's other still-open item (replicating
      the live block-damage *value* to nearby players) is a real prerequisite
      this doesn't have yet either.
- [x] Movement/action key bindings extended into the 6.3 keybind registry
      (6.19) — `move_forward`/`move_back`/`move_left`/`move_right`/`jump`/
      `sprint`/`primary`/`secondary` are pre-registered by every
      `PackRuntime` before any pack script runs, so a pack can read
      `input.keybinds["jump"]` etc. exactly like a custom keybind. Resolves
      the open design question: keyboard/mouse movement axes are already
      boolean (held/not-held), so they fit the existing boolean-keybind
      shape with no format change. Physical-key rebinding (the WASD/Space/
      Shift mapping) landed separately as Phase 5.3's keybindings screen,
      below — this item's own scope (name registry, not physical keys)
      is fully closed.
- [ ] HUD widgets aren't wired to `report_click`/`report_change` (display-only
      for now) — 6.16.
- [ ] Player list / chat box / hotbar are still hardcoded C++, not migrated
      to `ui.define_hud` — 6.16.
- [ ] No punch-rate cooldown enforced engine-side; no swing animation; PvP has
      no armor/cooldown/knockback — 6.18, deliberate scope cuts.
- [ ] Placed block is still a hardcoded `base_stone_id` in
      `content/base/mechanics.lua`, not read from a selected hotbar/
      inventory slot — 6.20; no "held item"/hotbar-selection primitive
      exists anywhere yet for a pack to read from.
- [x] **6.21:** block-edit reach and punch reach unified into one
      pack-overridable `net::ActionParams::reach` (`inc/vb/net/
      world_replicator.hpp`), replacing both `WorldReplicator`'s old
      hardcoded, non-overridable `kMaxReachBlocks` constant and
      `ServerSession::PunchParams::reach` — `WorldReplicator::set_reach()`/
      `reach()` hold the one value `in_reach()`/`apply_block_edit()` and
      `ServerSession::punch()` (via `world_replicator()->reach()`) all read.
      New `vb.action.set_params{reach=...}`/`vb.action.get_params()` Lua
      binding (`PackRuntime::effective_action_params`,
      `src/script/pack_runtime.cpp`) — deliberately its own namespace, not
      `vb.combat`, so a pack author hunting for the mining-reach knob doesn't
      have to think to search "combat" for it; `vb.combat.set_params` kept
      only its genuinely combat-specific fields. Also added
      `vb.physics.get_params()` (effective `MoveParams` as a plain table).
      `content/base/mechanics.lua`'s placing raycast now calls
      `vb.physics.get_params().eye_height`/`vb.action.get_params().reach`
      instead of its own hardcoded `EYE_HEIGHT`/`max_dist` constants. Wired
      into both `src/server/main.cpp` and `src/client/main.cpp`'s
      `--singleplayer` path, same config-then-pack-override precedent as
      `move_params`/`punch_params`. Tests: a new `blockedit_test.cpp` case
      proves `WorldReplicator::set_reach()` widens both block-edit reach and
      `punch()` reach from the same one call; new `pack_runtime_test.cpp`
      cases cover `vb.action.set_params` (override + post-freeze rejection +
      built-in default) and `vb.physics.get_params()`/`vb.action.get_params()`
      round-tripping the effective values. Full `vb_tests` green (294/294)
      on `build-net-lua`.

---

## Phase 7 — World & UX Polish (7.1-7.4 done, 7.5's default landed, override open)

> User-requested (2026-09-19): four UX gaps, scoped as their own phase since
> none of them extend Phase 6's "default + override" system pattern the way
> 6.1-6.20 did — these are new engine surfaces (loading UI, fog, liquid
> collision/vision/regions) plus closing a real content gap found while
> reviewing Phase 6 (`ui/pause.lua`/`ui/inventory.lua`'s own "nothing opens
> this yet" comments).

- [x] **7.1 — Engine-side loading screen with progress, during initial world
      load.** Landed 2026-09-22: a new `AppState::kLoading` in
      `src/client/main.cpp`, entered right after a successful join (instead
      of dropping straight into `kPlaying`) and left once the initial
      view-box of chunks has streamed in (or an 8-second
      `loading_deadline` elapses, so a server whose real `view_distance` is
      smaller than this client guessed never gets stuck at <100%). Stage 1:
      `MainMenu::draw_loading(fraction, operator_title)`
      (`src/render/main_menu.cpp`) draws a generic `GuiProgressBar` the
      instant the state is entered, no data dependency — `fraction` is
      `client->chunk_store().size()` over an expected count mirroring
      `WorldReplicator`'s own `chunks_in_view` box shape
      ((2·view_distance+1)²·7, vertical radius 3 hardcoded both in
      `src/net/world_replicator.cpp`'s `Singleplayer` construction and
      `src/server/main.cpp`), clamped to 1.0 so a real server's smaller box
      still reads "done". Stage 2: operator branding is **text only** — the
      existing `S2CServerInfo::motd` (already replicated during the
      handshake, `ClientSession::server_info()`) drawn above the bar once
      non-empty; **no color knob** was added (would need a new
      `ServerConfig`/`S2CServerInfo` field + protocol version bump, judged
      out of scope for this pass — a real future addition, not a cut
      corner). The `kLoading` state itself also pumps `sp->tick()`/
      `client->tick()` and `chunk_renderer->sync()` every frame (higher
      submit/upload budget than `kPlaying`'s steady-state 8/frame) — it's
      what's actually driving the load, not a passive wait. Verified: full
      `vb_tests` 294/294 green, clean `-Werror` build of both binaries on
      `build-net-lua`; the windowed state-machine path itself (a human
      watching the bar move) was **not** manually eyeballed this pass — see
      Phase 5's still-open "Live two-window manual playtest" item, same
      caveat applies here.
      Previously planned scope, explicitly **not** covered by the above:
      the connect-screen's own byte-progress bar (asset-sync fraction) is
      still text-only — asset-sync never grew progress-fraction accounting,
      so `kLoading`'s bar reads from *chunk* streaming only. Explicitly
      **not** Lua-driven (unlike 6.16's HUD/6.2's UI,
      which deliberately pushed presentation into content) — this covers the
      window between "connected" and "first playable frame" (asset sync +
      first-chunk load), before any `PackRuntime`/`UiRuntime` content is even
      guaranteed to have loaded, so it can't depend on a pack being present
      to draw it. Needs a real progress fraction to report — the connect
      screen's own still-open gap (Phase 5's "No connect-screen byte-progress
      bar (status-text-only) — asset-sync never grew progress-fraction
      accounting") is very likely a shared prerequisite; likewise chunk-load
      completion needs some "how many of my initial view-distance chunks are
      in" signal that doesn't currently exist as a queryable fraction.
      **Decided (2026-09-19), two stages, not one:** stage 1 is a fully
      generic bar (no data dependency at all) drawn the instant the loading
      window opens, so it never waits on anything network-derived; stage 2
      overlays lightweight **operator**-level branding (title text/color)
      read from `server.toml` once that config actually arrives, the same
      "operator, not pack, persona" split 6.13 already drew for
      `ServerConfig`. Deliberately **not** a pack/Lua-themeable surface at
      all, and not going any further than text+color — the point of this
      screen is getting out of the way of loading the real content quickly,
      not hosting a themable UI system.
- [x] **7.2 — Distance fog, adjustable from Lua.** Landed 2026-09-22: a real
      GLSL shader (`src/render/chunk_renderer.cpp`'s `kFogVs`/`kFogFs`, GLSL
      330, loaded once via `LoadShaderFromMemory` in `ChunkRenderer`'s
      constructor) replaces raylib's default mesh shader on every chunk's
      material — a faithful copy of it (same attribute/uniform names, so
      raylib's own `DrawMesh` keeps auto-wiring `mvp`/`matModel`/
      `colDiffuse`/`texture0` unchanged) plus a linear fog mix at the very
      end of the fragment shader. `ChunkRenderer::set_fog(view_pos, sky,
      start, end)` sets the `fogViewPos`/`fogColor`/`fogStart`/`fogEnd`
      uniforms once per frame from `src/client/main.cpp`'s `kPlaying` case,
      reusing the exact `SkyColor` already computed for `ClearBackground`
      that frame — fog color can never drift from the sky, by construction
      (matches the 2026-09-19 decision below).
      Wire protocol (bumped to **16**, `docs/protocol.md`): `S2CFogParams`
      (`inc/vb/protocol/world.hpp`, type 52) is `f32 fog_start, f32
      fog_end`, sent between `C2S_Ready` and `S2C_JoinAccept` alongside
      `S2C_MoveParams`/`S2C_DayNightCurve` only if
      `HandshakeServerHost::fog_params` returns a value
      (`PackRuntime::effective_fog_params()` on the server side, driven by a
      pack's `vb.render.set_fog{start=, ["end"]=}` — `end` needs a quoted
      key, it's a Lua reserved word). Unlike move_params/day_night_curve
      there's no server-side universal default this replaces: the server
      doesn't know each client's own `view_distance`, so `nullopt` (no pack
      override) means each client computes its own default from its own
      config instead (`src/client/main.cpp`: `fog_end = view_distance *
      kChunkDim`, `fog_start = fog_end * 0.6`) — `ClientSession::
      fog_override()` stays `std::optional`, not a struct with an
      always-valid default, to make that "no server value, client
      improvises" case explicit rather than a fake zero-initialized frame.
      No color field on the wire either, matching the decision below.
      Verified: full `vb_tests` 298/298 green (protocol round-trip +
      `PackRuntime` Lua-binding tests for `vb.render.set_fog`, including the
      `end <= start`/missing-field rejection cases), clean `-Werror` build
      of both binaries on `build-net-lua`, a `--headless --singleplayer`
      smoke run (never touches `ChunkRenderer`, so this only confirms the
      protocol-version bump and handshake didn't regress anything). The
      actual shader output (a human watching fog fade in near the edge of
      view distance in a real window) was **not** manually eyeballed this
      pass — no GUI in this agent environment, same still-open caveat as
      Phase 5's "Live two-window manual playtest" and 7.1's own loading-bar
      verification.
      **Decided (2026-09-19): fog color is never an independent Lua-settable
      field** — it's always whatever the current day/night sky color already
      is (6.8's `sky_color_for_time()`), so fog reads as "distance to the
      same sky," not a separate tint that can drift out of sync with it
      (e.g. green fog under a red sunset sky). Only the **distance**
      parameters are Lua-adjustable: an engine default (matching current
      `view_distance`) plus a `vb.render.set_fog{start=, end=}`-style
      override, following the same pre-freeze "default + override" shape and
      replication path as `S2C_MoveParams`/`S2C_DayNightCurve` (6.7/6.8) so a
      dedicated-server pack's choice reaches every client, not just
      `--singleplayer`.
- [x] **7.3 — Walkable "liquid" blocks (water): collision, underwater
      rendering (same sky-color fog mechanism, tighter distance), and a
      generic region-enter/exit hook for entity effects.** Landed
      2026-09-23. Collision needed no change — `is_solid` already gated
      `step_movement`'s AABB checks and `base:water` was already registered
      non-solid (Phase 2), so walking into/through it already worked;
      verified, not rebuilt.
      Underwater rendering: `src/client/main.cpp`'s `kPlaying` fog block
      (7.2) now computes the camera's own eye voxel (`controller.position()`
      floored) each frame and checks
      `client->chunk_store().registry().is_liquid(eye_block)` — when true it
      overrides whichever `fog_start`/`fog_end` were already chosen (engine
      default *or* a pack's `vb.render.set_fog` override alike) with a fixed
      close preset (`fog_start = 2.0f`, `fog_end = 8.0f`) before calling
      `ChunkRenderer::set_fog`. No separate tint/color system, exactly the
      2026-09-19 decision — underwater is 7.2's same sky-color fog mechanism,
      just a much closer distance, so surfacing restores normal visibility
      immediately with zero extra state to track.
      **Follow-up fix, same day:** the fog change alone didn't fully solve
      "can't see underwater" — `src/world/chunk_mesh_snapshot.cpp`'s face
      culling (`mesh_chunk_from_snapshot`) had always treated a liquid
      neighbour exactly like an opaque one (`blocks_face()`'s `is_opaque(id)
      || is_liquid(id)`), symmetrically, for *both* the block being meshed
      and its neighbour. That meant an opaque block's face touching water
      got culled too — a submerged block (seafloor, cave wall under a lake)
      was invisible from the water side even though nothing was actually
      drawn over it, a pre-existing bug (`tests/unit/mesher_test.cpp`'s old
      "a water block on solid ground only shows its top face" test even
      encoded it as expected behavior) that 7.2/7.3's fog work simply made
      visible for the first time. Fixed by making culling depend on the
      *current* voxel's own type, not just the neighbour's: a new
      `face_culled(current, outside)` culls on any opaque neighbour
      (unchanged), but only culls on a liquid neighbour when `current` is
      itself liquid (water-water merges into one body, matches the
      still-passing "liquid blocks cull faces against each other" test) —
      an opaque block's face is never culled by a liquid neighbour, and a
      liquid's own face is still culled by an opaque one below/beside it (no
      point drawing a submerged water face nobody can reach). `blocks_face()`
      itself is untouched and still used for AO sampling, where "is there
      occluding stuff here" is the right generic question regardless of the
      current voxel's own type. Updated the affected mesher test's name/
      comment/expected quad count (stone now keeps all 6 faces, was 5; total
      11, was 10) rather than leaving the old wrong-by-design assertion in
      place.
      **Second same-day follow-up fix (user-reported with a screenshot):**
      the mesher fix alone made the lake surface look "broken, like there
      are holes" when viewed from *above* — not a geometry bug this time.
      `src/render/chunk_renderer.cpp`'s `tint_for()` gave water `alpha=200`
      (semi-transparent), a pre-existing Phase-2-era value that was
      inconsequential as long as submerged terrain was always culled (there
      was never anything behind the water quad to blend with). Once the
      mesher fix above made that terrain actually render, the same alpha let
      the real sandy lakebed blend through — but as flat, hard-edged,
      voxel-shaped patches (no wave/refraction shading exists to sell a soft
      "shallow clear water" look), which read as corrupted geometry rather
      than water. Fixed by bumping water to `alpha=255`, opaque like every
      other block, so the lake surface is solid-looking from outside again;
      underwater visibility while swimming is untouched by this, since it's
      driven entirely by the fog system (once the camera's own eye voxel is
      inside the water) and never depended on this material's alpha.
      Generic region hook: `BlockType::region` (`inc/vb/world/block.hpp`) is
      a new flag independent of `liquid` — `BlockRegistry::base()` sets it on
      `base:water` only (`src/world/block.cpp`); `vb.register_block{region=}`
      defaults it to the block's own `liquid` value unless given explicitly
      (`src/script/pack_runtime.cpp`), so a pack's liquid opts in
      automatically and a non-liquid custom block (a future poison cloud)
      opts in on request. Server-side, `ServerSession::update_region_occupancy()`
      (`src/net/session.cpp`, called once per `tick()` after the punch-heal
      pass) reads each playing player's own position from the same
      `interest_` entry `update_item_drops` already reads, floors it to a
      voxel, and diffs `reg.is_region(block)` against a new
      `region_occupancy_` map keyed by `NetId` to fire
      `ServerSession::RegionHooks::enter`/`exit` exactly on the crossing —
      never once per tick spent inside one. Both hook fields are
      `std::function`s left unset (a no-op early return) unless a pack
      registered `vb.on("region_enter", ...)` or `"region_exit"`
      (`PackRuntime::attach_session`), same zero-extra-per-tick-cost posture
      as `BlockBreakHooks`. `PackRuntime::Impl::run_region_event` fires
      `vb.on("region_enter"/"region_exit", player, pos, block_name)` — the
      block is resolved to its registered *name* (via
      `replicator->world().registry()`) rather than a raw id, so a pack
      checks `block == "base:water"` instead of needing to know an id.
      **Explicitly out of scope, per user instruction:** no flowing-liquid
      physics/spread (Minecraft's water-source/flow-level simulation) — the
      block stays static once placed, only collision + visuals + the hook
      are in scope. **Position, not full AABB:** the occupancy check is a
      single-point test at the player's own position (matching every other
      per-player system in `ServerSession`, e.g. reach/item-pickup), not the
      player's full collision box — REMAINING_TASKS' original "position/AABB"
      note left this open; a point check was judged sufficient (water's
      collision box already visually matches the voxel) and simpler.
      Verified: full `vb_tests` 300/300 green — new coverage is
      `world_test.cpp`'s base-set `is_region` assertions,
      `pack_runtime_test.cpp`'s `register_block{region=}` default/override
      matrix, and a new `pack_runtime_integration_test.cpp` end-to-end case
      that moves a real player in and out of a real `base:water` voxel over
      a `LoopbackTransport` and confirms `region_enter`/`region_exit` each
      fire exactly once per crossing (via two counted `player:give()` calls
      gated on the passed block name), not once per tick spent inside;
      clean `-Werror` build of both binaries (temporarily reconfigured the
      local `build` dir with `-DVB_WARNINGS_AS_ERRORS=ON` — off by default
      for a plain local build, but what every CI workflow already passes —
      confirmed a clean rebuild of `vb_tests`/`voxel_browser`/
      `voxel_browser_server`, then reconfigured back to this dir's original
      OFF setting afterward). The actual underwater visual (a human swimming
      and seeing
      the closer fog kick in) was **not** manually eyeballed — no GUI in
      this agent environment, same still-open caveat as 7.1/7.2's own
      verification notes.
- [x] **7.4 — Wire `content/base`'s existing UI screens to a real trigger, as
      a working example.** Landed 2026-09-23. Root cause was one level deeper
      than "just add a keybind": Phase 6.3's `vb.register_keybind`/
      `S2C_KeybindRegistry` gives a pack a *named* bit in `InputCmd.keybinds`,
      but nothing on the client ever mapped a **physical key** to a
      pack-registered custom name — only the pre-registered engine names
      (movement + `primary`/`secondary`, Phase 6.19) got a real key via
      `MovementBindings`/`sample_input_cmd`. Two things landed together:
    - A new `kCustomKeybinds` table in `src/client/main.cpp` (`{"base:pause",
      KEY_ESCAPE}`, `{"base:inventory", KEY_E}`) — a minimal hardcoded
      default, not a real settings-screen UI (that's still a further, separate
      step past Phase 5.3's movement-only rebind screen). Read
      unconditionally in `sample_input_cmd` (unlike the engine-name lookups,
      which stay gated behind `mouse_captured`) via the same
      `set_engine_keybind` bit-setter, since opening a pause/inventory screen
      must work whether or not the mouse is currently captured for looking
      around.
    - A new `content/base/keybinds.lua` registering `"base:pause"`/
      `"base:inventory"` and a `vb.on("player_input", ...)` rising-edge
      handler calling `player:open_ui("base:pause", {})` /
      `player:open_ui("base:inventory", { slots = player:get_inventory() })`
      — the exact pattern `kitchen_sink/keybinds.lua` already demonstrated,
      applied to `content/base`'s own screens. `ui/pause.lua`/
      `ui/inventory.lua`'s stale "nothing opens this yet" header comments
      updated to point at the new wiring.
      Verified: full `vb_tests` 300/300 green (`content_pack_test.cpp`
      exercises the new `content/base/keybinds.lua` load as part of loading
      the whole pack), clean build of both binaries on `build-net-lua`. The
      actual keypress → screen-opens behavior (a human pressing Escape/E in a
      real window) was **not** manually eyeballed — no GUI in this agent
      environment, same still-open caveat as every other Phase 7 item's own
      verification note.
- **7.5 — Underwater fog tint should default to the liquid block's own
      color, overridable from Lua.** User-requested (2026-09-23), and
      **supersedes 7.2/7.3's 2026-09-19 decision** ("fog color is never an
      independent Lua-settable field... it's always whatever the current
      day/night sky color already is") for the underwater case specifically
      — that decision stays correct for *normal* (above-water) fog, but
      underwater fog tinted by the sky reads as wrong once you're actually
      submerged (water should tint the murk itself, not echo whatever color
      the sky happens to be at the time). Two parts:
    - [x] **Default**, landed 2026-09-23 alongside the real texture/atlas
      system (Phase 4's own item, `state/changelog-recent.md`): the
      underwater tint is now the **real average pixel color of `base:water`'s
      own synced texture** — `vb::render::TextureAtlas::build()` computes it
      once per session (decoding every block's texture and averaging its
      pixels, real `Image` data, not a guess) and `ChunkRenderer::
      underwater_tint(BlockId)` exposes it; `src/client/main.cpp`'s `kPlaying`
      block now passes that instead of `sky` to `set_fog()` whenever
      `is_liquid(eye_block)`. A liquid block with no texture (or before any
      atlas exists at all) still falls back to the old flat placeholder
      color exactly as this item originally scoped as its own interim step —
      that fallback is `vb::render::fallback_color_for()` now (moved out of
      `chunk_renderer.cpp`'s old `tint_for()`, same values, renamed).
    - [ ] **Override** — still open: extend `vb.render.set_fog{...}` (or a
      sibling call) to accept an underwater-specific color, replicated the
      same `S2CFogParams`-style opt-in path as `fog_start`/`fog_end` (6.7/
      6.8's "default + override" shape) — needs a protocol version bump
      (current `ENGINE_PROTOCOL_VERSION` is **17**) since `S2CFogParams`
      (`inc/vb/protocol/world.hpp`, type 52) is `f32 fog_start, f32 fog_end`
      only today, no color field.
      Design not otherwise pinned yet (exact Lua call shape, whether the
      override is per-block-name or a single global underwater tint,
      whether normal above-water fog keeps the sky-only rule unconditionally
      or also becomes overridable) — decide during implementation.

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
- [ ] Address the remaining open item(s) in `ARCHITECTURE_SPEC.md` §18 as
      their blocking phase arrives (renumbered from §19; most rows are
      already resolved or have a noted direction — Q4 chunk compression is
      the one still fully open; Q5 persistence and Q6 auth have a direction
      set but aren't implemented yet); record decisions in that section.

---

## Deferred (post first-playable)

World persistence (region files, save/load, disk eviction) · token auth
verification · audio subsystem + Lua sfx/music API · server-side plugin
hot-reload · entity-entity physics/mounts/projectiles · particle system
beyond block-break puffs · compression tuning (zstd, snapshot deltas,
bit-packed inputs) · dedicated server browser/master list · modding
(stacked packs, dependency resolution) · rule-based decorative structure
placement for worldgen (depends on 6.14, which landed the prerequisite —
this item itself not attempted) · Voronoi biome-cell resolution caching
(6.14, no perf problem observed yet) · CSS-like declarative layout for
`UiRuntime` widgets (today: absolute pixel positioning only).

Full reasoning for each: `remaining_tasks/deferred.md`.
