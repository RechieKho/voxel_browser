## Phase 7 — World & UX Polish

> Full history for this phase; linked from `REMAINING_TASKS.md`. Ground truth for [x] items — do not duplicate here.

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
      player's full collision box — a point check was judged sufficient
      (water's collision box already visually matches the voxel) and simpler.
      Verified: full `vb_tests` 300/300 green — new coverage is
      `world_test.cpp`'s base-set `is_region` assertions,
      `pack_runtime_test.cpp`'s `register_block{region=}` default/override
      matrix, and a new `pack_runtime_integration_test.cpp` end-to-end case
      that moves a real player in and out of a real `base:water` voxel over
      a `LoopbackTransport` and confirms `region_enter`/`region_exit` each
      fire exactly once per crossing (via two counted `player:give()` calls
      gated on the passed block name), not once per tick spent inside;
      clean `-Werror` build of both binaries (temporarily reconfigured the
      local `build` dir with `-DVB_WARNINGS_AS_ERRORS=ON`, confirmed a clean
      rebuild of `vb_tests`/`voxel_browser`/`voxel_browser_server`, then
      reconfigured back to this dir's original OFF setting afterward). The
      actual underwater visual (a human swimming and seeing the closer fog
      kick in) was **not** manually eyeballed — no GUI in this agent
      environment, same still-open caveat as 7.1/7.2's own verification
      notes.
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
- [x] **7.5 — Underwater fog tint should default to the liquid block's own
      color, overridable from Lua.** User-requested (2026-09-23), and
      **supersedes 7.2/7.3's 2026-09-19 decision** ("fog color is never an
      independent Lua-settable field... it's always whatever the current
      day/night sky color already is") for the underwater case specifically
      — that decision stays correct for *normal* (above-water) fog, but
      underwater fog tinted by the sky reads as wrong once you're actually
      submerged (water should tint the murk itself, not echo whatever color
      the sky happens to be at the time). Two parts:
    - [x] **Default**, landed 2026-09-23 alongside the real texture/atlas
      system (Phase 4's own item): the underwater tint is now the **real
      average pixel color of `base:water`'s own synced texture** —
      `vb::render::TextureAtlas::build()` computes it once per session
      (decoding every block's texture and averaging its pixels, real
      `Image` data, not a guess) and `ChunkRenderer::underwater_tint(BlockId)`
      exposes it; `src/client/main.cpp`'s `kPlaying` block now passes that
      instead of `sky` to `set_fog()` whenever `is_liquid(eye_block)`. A
      liquid block with no texture (or before any atlas exists at all)
      still falls back to the old flat placeholder color exactly as this
      item originally scoped as its own interim step — that fallback is
      `vb::render::fallback_color_for()` now (moved out of
      `chunk_renderer.cpp`'s old `tint_for()`, same values, renamed).
    - [x] **Override**, landed 2026-09-27: `vb.render.set_fog{start=,
      ["end"]=, underwater_tint={r=, g=, b=}}` — `underwater_tint` is a new,
      independently-optional field on the same call, each channel `0-255`,
      validated the same way a bad `start`/`end` already was (all three
      channels required if the table is given at all; out-of-range rejects
      the whole call). Protocol bumped **23 -> 24**: `S2CFogParams` (type 52)
      gains `bool has_underwater_tint` followed by, only if true, `u8
      underwater_tint_r/g/b` — `has_underwater_tint = false` means "no
      override, client keeps its own texture-average/placeholder default,"
      not "black," same "absence is not a value" posture `fog_start`/
      `fog_end` already had. `src/client/main.cpp`'s underwater branch checks
      `client->fog_override()->has_underwater_tint` first and only falls back
      to `ChunkRenderer::underwater_tint()`'s texture-average default when a
      pack never set one. Above-water fog color is untouched and still never
      independently settable (2026-09-19's decision, unchanged) — this is
      strictly the underwater-only exception 7.5 always scoped it as.
      Verified: full `vb_tests` 350/350 green (a `protocol_test.cpp`
      round-trip case with the tint set, `pack_runtime_test.cpp` cases for
      the override applying/being absent/being rejected on a missing channel
      or an out-of-range one), clean `-Werror` build of `vb_tests`/
      `voxel_browser`/`voxel_browser_server`. The actual rendered tint swap
      (a human swimming with a pack-set override active) was **not**
      manually eyeballed — no GUI in this agent environment.
- [x] **7.6 — World persistence: chunks survive a server restart.** Landed
      2026-09-25, reversing ARCHITECTURE_SPEC.md §18 row 5's 2026-09-17
      "lean toward LMDB" direction note after review (see that section for the
      updated status) in favor of flat per-region files -- no new
      `FetchContent` dependency, reuses the existing palette+RLE chunk codec
      as-is. New `vb::world::RegionStore` (`inc/vb/world/region_store.hpp`,
      `src/world/region_store.cpp`) groups chunks into one file per X/Z region
      (16x16 chunks; Y is never grouped -- generated worlds here are only a
      few chunks tall, unlike Minecraft's motivating case). Only *edited*
      chunks are ever persisted: `Chunk::revision() == 0` means "still exactly
      what worldgen produced," so regenerating on next load is equivalent and
      cheaper than reading+writing it; a chunk already cached/on-disk at its
      current revision is never re-encoded either. Writes are batched:
      `save_if_dirty()` only touches an in-memory per-region cache,
      `flush()` is the one call that actually rewrites a dirty region file.
      `ChunkLifecycleSystem` takes an optional `RegionStore*` (nullptr =
      disabled, same posture as every other opt-in engine seam): `update()`'s
      request step loads a wanted chunk from disk instead of submitting it to
      worldgen if one was saved there, and its unload step saves an edited
      chunk before evicting it. `WorldReplicator::set_region_store()` forwards
      straight through. `voxel_browser_server`'s `main.cpp` owns the
      `RegionStore` (nullptr when `server.toml`'s new `persist_world = false`),
      wires it into the replicator, sweeps every loaded chunk through
      `save_if_dirty()` + one `flush()` on a config'd `autosave_interval_seconds`
      cadence (default 60s; 0 disables periodic autosave, edits still save on
      chunk unload) and unconditionally once more right before the process
      exits. Three new `server.toml` keys: `persist_world` (default `true`),
      `world_dir` (default `"world"`, relative like `content_pack`),
      `autosave_interval_seconds` (default `60.0`).
      **Deliberately out of scope at the time, per the AskUserQuestion
      decision that shaped this pass:** no LZ4/zstd framing on region files
      (chunk codec's own RLE was the only compression here) -- landed
      2026-09-28, see `remaining_tasks/cross_cutting.md` and
      `ARCHITECTURE_SPEC.md` §18 row 5 for the writeup.
      `--singleplayer`'s in-process integrated server (`src/client/main.cpp`)
      was **not** wired to a `RegionStore` at all when this phase first
      landed -- closed 2026-09-28 as its own follow-up, see `STATE.md`'s
      "Current status" for the full writeup (`Singleplayer` now owns a
      `RegionStore` under a fixed `world_singleplayer/` directory, no
      `client.toml` toggle to disable it yet). A corrupt/unreadable
      region file is logged (`VB_WARN`) and treated as "chunk never saved"
      (regenerates from worldgen), never fatal -- no migration/versioning
      story exists yet for a region file format change (version 1 today).
      Verified: full `vb_tests` 312/312 green (5 new cases in
      `region_store_test.cpp`, including one that drives a real
      `ChunkLifecycleSystem` through `WorldReplicator` end-to-end -- edit a
      block, walk far enough to unload+save the chunk, walk back and confirm
      it's loaded from disk with the edit intact rather than regenerated),
      clean `-Werror` build of `vb_tests`/`voxel_browser`/
      `voxel_browser_server`. Also manually ran
      `voxel_browser_server.exe --ticks 40` against a fresh working directory
      with no client ever connecting: confirmed no `world/` directory is
      created at all when nothing was ever edited (matches the "only persist
      edits" design, not a missed case).
