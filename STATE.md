# STATE — Working Notes for Future Agents

> Living scratchpad of gotchas, landmines, open decisions, and small TODOs
> discovered while working in this repo. **Update this file** when you learn
> something non-obvious or fix something listed here. Keep this file lean
> (~300-400 lines) — new verbose writeups belong in `state/*.md`, linked from
> here, not pasted inline. See "Detail files" at the bottom for the index.
>
> Companion docs: `ARCHITECTURE_SPEC.md` (target design) · `REMAINING_TASKS.md`
> (implementation backlog). This file is for *traps and context*, not the plan.
>
> **If what you learned is tied to a specific physical machine** (a tool
> path, an agent-shell/Bash-vs-PowerShell quirk, which pre-built `build-*`
> dirs exist, a local toolchain oddity) **write it to `STATE.md.local`
> instead of here** — see that file's own header for why. This file (and
> `state/*.md`) is for gotchas that hold regardless of which machine an
> agent is running on.

---

## Current status (2026-09-25)

**Same-day follow-up #5: automatic despawn-on-health for generic script
entities** (REMAINING_TASKS' Phase 6.1 "no health primitive exists; a pack
tracks HP on `self` itself" gap). Opt-in per kind via
`vb.register_entity{health=...}` (new `EntityKindDef::max_health`, rejects
`health <= 0` at registration time) — a kind that never sets it is completely
unaffected (`entity:damage()` stays notification-only: fires `on_hit`,
nothing else, exactly as before this landed). A kind that opts in gets a
per-instance current health (`ScriptEntity::health`, seeded from the kind
default in `vb.world.spawn`), new `entity:get_health()` (returns
`{current, max}` or `nil` if untracked) / `entity:set_health(value)` (clamped
`[0, max]`, errors if untracked) accessors, and `entity:damage()` now
decrements the tracked value and calls the existing `despawn_entity()` (fires
`on_death`, deregisters from the interest grid) once it reaches 0 -- the same
path `entity:remove()` already used, just triggered automatically. Server-
side bookkeeping only, never replicated (no client HUD reads a script
entity's health, so no protocol version bump).
**Gotcha hit while wiring this up, worth knowing before touching
`dispatch_entity_hit` again:** `on_hit` is arbitrary pack Lua and can itself
call `self:remove()` -- kitchen_sink's `entities/sentry.lua` does exactly
this, tracking its own hand-rolled hp on `self` rather than the new engine
primitive. The first draft found the `ScriptEntity` iterator once, fired
`on_hit`, then kept using that same iterator to touch `.health` -- a
use-after-erase the instant `on_hit` despawns the entity itself, since
`despawn_entity()` erases the map entry out from under it.
`kitchen_sink_pack_test.cpp`'s existing sentry test caught this immediately
(an MSVC STL iterator-debug assertion, not a silent corruption) the first
time the changed `pack_runtime.cpp` was rebuilt and the full suite run --
confirmed by `git stash`-ing back to the pre-change tree and re-running the
identical test in isolation, which passed clean. Fixed by re-`find`ing the
entity by id *after* the `on_hit` call returns, instead of reusing the
pre-call iterator. Takeaway: any C++ code here that calls into pack Lua and
then wants to keep touching the same engine-side entity/map entry afterward
must assume the Lua call may have deleted that very entry, and re-look it up
-- `dispatch_entity_tick`'s per-tick loop already re-`find`s for exactly this
reason (its own comment: "removed by an earlier handler this tick"), but
`dispatch_entity_hit` had not been paying that same tax until this pass.
Verified: full `vb_tests` 319/319 green (a new
`pack_runtime_integration_test.cpp` case spawns a `health=5` kind next to an
opted-out kind, proves 2 hits of 3 auto-despawns the tracked one at exactly 0
with no explicit `:remove()` call while the untracked one survives 1000
damage notification-only; a `pack_runtime_test.cpp` case covers the
registration-time `health <= 0` rejection), clean `-Werror` build of
`vb_tests`/`voxel_browser`/`voxel_browser_server` (temporarily reconfigured
`build-net-lua` with `-DVB_WARNINGS_AS_ERRORS=ON`, confirmed clean,
reconfigured back to this dir's OFF default afterward -- same verification
pattern as every other recent pass).

**Same-day follow-up #4: client-side kind-specific rendering for script
entities** (REMAINING_TASKS' Phase 6.1 "EntityKind id threads through but
nothing branches on it" gap). Protocol version bumped **18 -> 19**: new
`S2C_EntityKindRegistry` (type 53, `inc/vb/protocol/world.hpp`) carries
`{name, width, height}` per `vb.register_entity` kind, `kinds[i]` describing
`EntityKindId i+1` (matches `PackRuntime::Impl::entity_kinds`' own
registration-order id assignment, so no id needs to ride the wire). Sent
between `C2S_Ready` and `S2C_JoinAccept` alongside `S2C_BlockRegistry`/
`S2C_KeybindRegistry` via a new `HandshakeServerHost::entity_kind_registry`
hook, same `nullopt` = no frame, no behavior change posture as every other
opt-in registry here. `PackRuntime::install_entity_kind_registry(host)`
mirrors `install_keybind_registry`'s shape exactly; wired into both
`src/server/main.cpp` and `src/client/main.cpp`'s `--singleplayer` host
(alongside `install_join_veto`, mirroring `host.block_registry`'s presence in
both paths). `vb.register_entity{width=, height=}` (new optional fields on
`EntityKindDef`, default `0.8`/`1.8` -- `render::EntityRenderer`'s own
existing placeholder quad dimensions) lets a pack size a kind's billboard;
`ClientSession::entity_kind(EntityKindId)` looks a script entity's record up
by id (`nullptr` for `kInvalid`/players or an id with no registry entry --
same "missing = default" fallback as everywhere else), and
`EntityRenderer::sync()` now re-checks it every frame per tracked entity
(cheap, one map lookup) to pick the billboard's width/height, replacing the
one flat `kPlaceholderWidth`/`kPlaceholderHeight` every kind used to render
as regardless of what it was. Players (`EntityRecord::kind == kInvalid`)
still always get the placeholder default. `content/examples/kitchen_sink`'s
`entities/sentry.lua` demonstrates the new fields (`width=1.0, height=1.2`).
**Deliberately not attempted:** real per-kind sprite art/atlas
(`visual = {...}`, REMAINING_TASKS' own still-open item) -- this only closes
the "nothing branches on kind at all" gap with a differently-sized flat
placeholder, not real art. Full `vb_tests` 317/317 green on `build-net-lua`
(5 new cases: a protocol round-trip + cap-free-list test, two
`block_registry_test.cpp` end-to-end client-applies-it/no-hook-means-empty
cases mirroring the existing keybind-registry pair, two `pack_runtime_test.cpp`
cases for `install_entity_kind_registry`'s built/empty output), clean
`-Werror` build of `vb_tests`/`voxel_browser`/`voxel_browser_server`
(temporarily reconfigured `build-net-lua` with `-DVB_WARNINGS_AS_ERRORS=ON`,
confirmed clean, reconfigured back to this dir's OFF default afterward --
same verification pattern as every other recent phase). The actual rendered
size difference (a human watching two different-sized billboards in a real
window) was **not** manually eyeballed -- no GUI in this agent environment,
same still-open caveat as every other recent Phase 6/7 item's own
verification note.

**Same-day follow-up #3: closed Phase 3's two remaining ECS items --
`vb::ecs::SystemRunner` (§7.2) + the client-side lightweight registry.**
`SystemRunner` (`inc/vb/ecs/system_runner.hpp`/`src/ecs/system_runner.cpp`,
new `vb/ecs` module) is a named, ordered list of tick phases;
`ServerSession::build_systems()` registers `tick()`'s existing phases under
it plus one new one, `system_sync_interest()`. Script entities (Phase 6.1)
are now real registry entities (`spawn_script_entity`/
`set_script_entity_state`/`remove_script_entity` write `Position`/
`EntityKind`/`NetReplicated`/`Rotation`/`Velocity` components instead of
only touching the interest grid), giving the runner a genuine second
consumer besides players -- `system_sync_interest()` generically pushes
every such entity into `interest_` once a tick
(`registry_.view<Position, NetReplicated>(exclude<PlayerTag>)`).
Client-side: `ClientSession`'s old bespoke `RemoteSample`/`remote_samples_`
interp bookkeeping is now a real `entt::registry` + the
already-defined-but-previously-unused `ecs::InterpBuffer` component.
`remote_entities()`/`interpolated_pos()`'s public API is unchanged (3 test
files + `entity_renderer.cpp` needed no edits). See
`remaining_tasks/phase3.md` for the full write-up.

**Gotcha hit and fixed during this pass, worth knowing before touching
`ServerSession::tick()` again:** `interest_` (the `replication::InterestGrid`)
and `registry_` (the EnTT registry) are *not* interchangeable "current
position" sources within a single tick, even though both eventually hold the
same value. Several `network_io`-phase handlers -- `handle_block_edit`,
`handle_block_break_begin`, `punch()` (called synchronously from a
`vb.on("player_input", ...)` Lua hook, i.e. *during* `handle_input_batch`) --
need a player's *just-simulated-this-tick* position/rotation for reach/hit
checks. The first attempt at `system_sync_interest()` made it fully generic
(all `Position+NetReplicated` entities, players included) and moved it to a
single once-per-tick pass after `network_io` -- this silently broke reach
checks (a block breaks only if still in range *after* the input that moved
the player toward it, but the edit message can arrive in the same batch as
that move) and 2 of 312 tests failed on it. Fix: players keep their original
*immediate* `interest_.upsert()` calls (`handle_input_batch`,
`check_respawns`, `set_player_state`, join) exactly as before;
`system_sync_interest()` is scoped to script entities only (`exclude
<PlayerTag>`), which have no such synchronous same-tick reader. The 4 reach-
sensitive handlers above were also changed to read `ecs::Position`/
`ecs::Rotation` straight off `registry_` instead of `interest_`, since that's
always live regardless of which pass has run. Takeaway: `registry_` is the
one true "right now" source; `interest_` is a replication-facing mirror that
different entity kinds are allowed to refresh on different cadences (players:
immediately; script entities: once/tick) as long as every *reader* is aware
of which cadence it's getting.

**Same-day follow-up #2: fixed the multiplayer loading screen dismissing over
a still-empty world again** (user-reported regression, same symptom the
original Phase 7.1 loading-screen bug had). Root cause: Phase 7.6's world
persistence (below) added a *second*, unbudgeted synchronous-work path into
`ChunkLifecycleSystem::update()` -- step 2's saved-chunk disk load+relight
loop iterates the entire `desired` view box calling `region_store_->load()`
synchronously with no cap, unlike step 1's `ingest_budget_`-bounded worldgen
ingest right above it. Rejoining a previously-saved world with a real
`voxel_browser_server` re-loads its *entire* initial view box (~2000 chunks
at view_distance=8) from disk in one `update()` call, blocking that tick (and
the network send that follows it) long enough that the client's 5s
loading-screen stall deadline fires before any chunks arrive. Fixed by
sharing `ingest_budget_` across both loops: step 2 now stops after
`ingest_budget_` disk-load attempts (hit or miss) per `update()` call and
leaves the rest for a later tick, instead of falling through to
`pool_.submit()` for a deferred coord (which would silently regenerate --
and discard -- a chunk that actually has saved data). Only fires when
`persist_world` is on and rejoining a world with existing saves; a fresh
world or `persist_world=false` is unaffected (matches `region_store_ ==
nullptr` early-outs already in place). `vb_tests` 312/312 green on
`build-net-lua`. See `src/world/chunk_lifecycle.cpp`'s step-2 comment.

**Same-day follow-up: fixed a singleplayer FPS dip during chunk streaming**
(user-reported: "the frame rate dipped" while chunks loaded, distinct from
the 2026-09-15 dip already fixed in `chunk_mesh_snapshot.cpp`). Root cause:
`ChunkLifecycleSystem::update()`'s `kIngestBudgetPerTick` (32 chunks
inserted+relit per call, `src/world/chunk_lifecycle.cpp`) was sized assuming
one `update()` call per real tick interval -- true for a dedicated server,
which sleeps between ticks, but not for `--singleplayer`'s
`Singleplayer::tick()` (`src/client/main.cpp`), which runs a fixed-step
catch-up loop of up to `kMaxStepsPerFrame` (5) server ticks inside a *single*
rendered frame after any stall. Each step called `update()` with the full
budget, so a stall could relight up to 5x32=160 chunks synchronously in one
frame -- expensive enough (flood-fill sky+block light over ~32k voxels each)
to cause the next frame's stall too, a self-sustaining stutter. Fixed with
`ChunkLifecycleSystem::set_ingest_budget()` / `WorldReplicator::
set_chunk_ingest_budget()` passthrough (new, both header-only setters);
`Singleplayer::tick()` now computes `expected_steps` up front (pure function
of `tick_accum_/kFixedDt`, capped the same as the loop below it) and divides
the base budget across them before the catch-up loop runs, so one frame's
total ingest work stays bounded to roughly the original per-tick budget
regardless of how many steps it catches up on. A dedicated server never
calls `set_chunk_ingest_budget()` at all, so its behavior (and the existing
`kIngestBudgetPerTick`-sized test expectations) is unchanged. Full
`vb_tests` 312/312 green, clean `voxel_browser`/`voxel_browser_server`
rebuild, on `build-net-lua`. **Not fixed by this pass** (separate, still-open
item, see §6 below): `WorldReplicator::tick()`'s per-player chunk *encode+
send* loop is still an uncapped burst with no byte budget of its own --
today's fix only bounds the ingest/relight side.

Most recent landed item is **Phase 7.6: world persistence** (chunks now
survive a `voxel_browser_server` restart), closing ARCHITECTURE_SPEC.md
§18 row 5. **Reversed that row's 2026-09-17 "lean toward LMDB" direction
note** after an AskUserQuestion with the user during this pass — landed as
flat per-region files instead, no new dependency. New `vb::world::RegionStore`
(`inc/vb/world/region_store.hpp`/`src/world/region_store.cpp`) groups chunks
into one file per 16x16-chunk X/Z region (Y ungrouped -- this engine's
generated worlds are only a few chunks tall, unlike Minecraft's motivating
case), reusing the existing `chunk_codec.hpp` palette+RLE payload as-is. Only
*edited* chunks are ever written: `Chunk::revision() == 0` (untouched since
worldgen) is skipped, and a chunk already cached/on-disk at its current
revision is never re-encoded -- `save_if_dirty()` only updates an in-memory
per-region cache, `flush()` is the one call that actually rewrites a dirty
region file, so many edits to the same region across a sweep cost one disk
write, not one per chunk. `ChunkLifecycleSystem` takes an optional
`RegionStore*` (nullptr = disabled, the usual opt-in-seam posture in this
codebase): its request step loads a wanted chunk from disk instead of
submitting it to worldgen when one was saved there, its unload step saves an
edited chunk before evicting it. `WorldReplicator::set_region_store()`
forwards straight through to it. `src/server/main.cpp` owns the `RegionStore`
(three new `server.toml` keys: `persist_world` default `true`, `world_dir`
default `"world"`, `autosave_interval_seconds` default `60.0`), sweeps every
loaded chunk through it on that interval and unconditionally once more right
before exiting. **Gotcha hit while writing this:** `RegionStore` needed its
own local copy of the `read_whole_file()` idiom (ifstream + `ate`/`tellg` +
manual `vector<byte>` fill) -- `std::vector<std::byte>` still can't be built
directly from `std::istreambuf_iterator<char>` under this repo's toolchain
(same §4 gotcha `assetsync/cache.cpp` and `script/db.cpp` already worked
around independently; there's no shared helper to import, `assetsync` isn't a
dependency of the `vb::world` module). **Deliberately not covered:**
`--singleplayer`'s in-process integrated server has no `RegionStore` wired in
at all (it has no `ServerConfig`/`server.toml` on that path to begin with --
see `make_singleplayer_pack_runtime`'s own comment in `src/client/main.cpp`),
and region files carry no LZ4/zstd framing yet (RLE only) -- both tracked as
their own `remaining_tasks/deferred.md` items now, not silently dropped. Full
`vb_tests` 312/312 green (5 new `region_store_test.cpp` cases, one of them a
real `ChunkLifecycleSystem`-through-`WorldReplicator` end-to-end round trip:
edit a block, walk far enough to unload+save it, walk back and confirm it
loads from disk with the edit intact instead of regenerating), clean
`-Werror` build of `vb_tests`/`voxel_browser`/`voxel_browser_server`
(temporarily reconfigured `build-net-lua` with `-DVB_WARNINGS_AS_ERRORS=ON`,
confirmed clean, reconfigured back to this dir's OFF default afterward).
Also manually ran `voxel_browser_server.exe --ticks 40` against a scratch
working directory with no client connecting: confirmed no `world/` directory
gets created at all when nothing was ever edited (the "only persist edits"
design working as intended, not an oversight). Full REMAINING_TASKS.md
write-up under Phase 7's new 7.6 entry.

Before that, most recent landed item was the **real texture/atlas system**, closing the
"no real texture system in this engine at all" gap REMAINING_TASKS.md
repeatedly cited as blocking Phase 4's textured meshing, 6.5's crack
overlay, 5's sprite atlases, and 7.5's underwater tint default.
`kEngineProtocolVersion` bumped 16 -> 17 (`texture` field added to
`BlockRegistryRecord`/`BlockType`). New `vb::render::TextureAtlas`
(`inc/vb/render/texture_atlas.hpp`) decodes each block's synced PNG via
raylib `Image` functions and packs one fixed 16x16 cell per block id into a
single atlas, built once per session in `src/client/main.cpp` right after
the block registry is applied. **Gotcha found while wiring this up:**
`BlockRegistry::add_or_get` silently discards the `BlockType` it's passed
when the name already exists (by design — idempotent re-registration must
not reset an earlier call's fields) — so a pack file re-declaring an
already-hardcoded `BlockRegistry::base()` block (this pass's
`content/base/blocks/stone.lua`/`water.lua`, attaching a texture to blocks
`src/world/block.cpp` already defines) would silently no-op its `texture`
too. Fixed with a narrow `BlockRegistry::set_texture(id, path)`, the one
field this rule doesn't apply to — don't add more fields to that exception
without re-checking every pack that relies on "first registration's values
win" (`pack_runtime_test.cpp`'s idempotent-registration case is the
regression guard). Full write-up: `state/changelog-recent.md`. Full
`vb_tests` 306/306 green, clean build of both binaries, all 4 CTest cases
pass, on `build-net-lua`.

Before that, most recent landed item was **Phase 7.4: wire `content/base`'s UI screens to a
real trigger**. Root cause was one level deeper than "just add a keybind":
Phase 6.3's `vb.register_keybind`/`S2C_KeybindRegistry` gives a pack a
*named* bit in `InputCmd.keybinds`, but nothing on the client ever mapped a
**physical key** to a pack-registered custom name — only the pre-registered
engine names (movement + `primary`/`secondary`, Phase 6.19) got a real key.
Fixed with a new `kCustomKeybinds` table in `src/client/main.cpp`
(`{"base:pause", KEY_ESCAPE}`, `{"base:inventory", KEY_E}`) read
unconditionally in `sample_input_cmd` (not gated behind `mouse_captured`,
unlike the engine-name lookups — opening a menu must work whether or not the
mouse is currently captured), plus a new `content/base/keybinds.lua`
registering those two names and a `vb.on("player_input", ...)` rising-edge
handler calling `player:open_ui("base:pause", {})` /
`player:open_ui("base:inventory", { slots = player:get_inventory() })` — same
pattern `content/examples/kitchen_sink/keybinds.lua` already demonstrated.
`ui/pause.lua`/`ui/inventory.lua`'s stale "nothing opens this yet" header
comments updated. Still just a hardcoded default mapping, not a real
settings-screen UI for custom keybinds (Phase 5.3's rebind screen only
covers the 6 `MovementBindings` axes) — that's a further, separate step.
Full `vb_tests` 300/300 green on `build-net-lua`; the actual keypress →
screen-opens behavior wasn't manually eyeballed (no GUI in this agent
environment). Full REMAINING_TASKS.md write-up under Phase 7's 7.4 entry.

Before that, most recent landed item was **Phase 7.3: walkable liquid blocks** — collision
was already correct (verified, not rebuilt: `is_solid` already gated
`step_movement`, `base:water` was already non-solid); underwater rendering
is 7.2's same sky-color fog mechanism with a fixed close preset
(`fog_start=2.0f`/`fog_end=8.0f`) overriding whichever fog distance was
already chosen, triggered in `src/client/main.cpp`'s `kPlaying` block
whenever `client->chunk_store().registry().is_liquid()` is true for the
camera's own floored eye position (**same-day follow-up fix:** the fog
change alone wasn't enough — `src/world/chunk_mesh_snapshot.cpp`'s face
culling had always symmetrically treated a liquid neighbour like an opaque
one, so an opaque block's face touching water was culled too, making
submerged terrain invisible from the water side, a pre-existing bug the old
`mesher_test.cpp` even asserted as expected; fixed by keying culling off the
*current* voxel's own type — a liquid neighbour now only culls another
liquid's own face, never an opaque block's — see REMAINING_TASKS.md 7.3's
entry for the full writeup); and a new generic `BlockType::region`
flag (`base:water` only in the base set; `vb.register_block{region=}`
defaults to the block's own `liquid` value) drives
`ServerSession::update_region_occupancy()` (`src/net/session.cpp`, one new
per-tick pass reading the same `interest_` position source
`update_item_drops` uses, diffed against a `NetId`-keyed
`region_occupancy_` map) firing `vb.on("region_enter"/"region_exit", player,
pos, block_name)` exactly on the crossing, not per tick spent inside — no
wire-protocol change, this is server-local Lua dispatch only, gated
zero-cost behind `ServerSession::RegionHooks` (unset function fields = no-op)
the same way `BlockBreakHooks` already is. No engine-side swim-speed/
movement-slowdown policy was added — that's left entirely to a pack built on
top of the hook, same "engine provides the primitive" posture as everything
else in Phase 6/7. **Second same-day follow-up fix:** the mesher fix alone
made the lake surface look "broken, like there are holes" from *above*
(user-reported, screenshot-confirmed) — real cause was
`src/render/chunk_renderer.cpp`'s `tint_for()` giving water `alpha=200`
(semi-transparent), a Phase-2-era value that was inert as long as submerged
terrain was always culled (nothing behind it to blend with). Once the mesher
fix made that terrain render, the same alpha let the sandy lakebed blend
through as flat, hard-edged, chunk/voxel-shaped patches — no wave/refraction
shading exists to sell it as water, so it read as corrupted geometry rather
than "shallow clear water." Fixed by bumping water to `alpha=255`, opaque
like every other block; underwater visibility while swimming is untouched,
it's driven entirely by the fog system once the camera's own eye voxel is
inside the water, not by this material's alpha.
Full `vb_tests` 300/300 green on this machine's plain
`build` dir (both `VB_WITH_NET`/`VB_WITH_LUA` ON); also re-verified a clean
`-Werror` build (`-DVB_WARNINGS_AS_ERRORS=ON`, matching every CI workflow)
of `vb_tests`/`voxel_browser`/`voxel_browser_server` before reconfiguring
back to this dir's plain-local OFF default. Full REMAINING_TASKS.md
write-up under Phase 7's 7.3 entry.

Before that, most recent landed item was **Phase 7.2: distance fog**, protocol version
bumped to **16**. A real GLSL 330 shader (`kFogVs`/`kFogFs` in
`src/render/chunk_renderer.cpp`, loaded once in `ChunkRenderer`'s
constructor) replaces raylib's default mesh shader on every chunk's
material — attribute/uniform names match raylib's own defaults exactly
(`vertexPosition`/`mvp`/`matModel`/`colDiffuse`/`texture0`) so `DrawMesh`
keeps auto-wiring those; only the `fogViewPos`/`fogColor`/`fogStart`/
`fogEnd` uniforms and a linear mix at the end of the fragment shader are
new. `ChunkRenderer::set_fog(view_pos, sky, start, end)` is called once per
frame from `src/client/main.cpp`'s `kPlaying` case with the exact `SkyColor`
already computed for that frame's `ClearBackground` — fog can't
independently drift from the sky by construction. New wire message
`S2C_FogParams` (type 52, `f32 fog_start, f32 fog_end`) is opt-in like
`S2C_MoveParams`/`S2C_DayNightCurve`, but with no server-side universal
default to fall back to (the server doesn't know each client's own
`view_distance`) — `ClientSession::fog_override()` is `std::optional`, and a
client with none computes its own default from `view_distance * kChunkDim`.
Lua: `vb.render.set_fog{start=, ["end"]=}` (`PackRuntime::
effective_fog_params()`) — note `end` needs a quoted key, it's a Lua
reserved word (caught the hard way: the first test-case draft used a bare
`end = ...` key and failed at Lua parse time, not at the C++ validation it
was meant to exercise). No color field, by design (matches 6.8's "operator/
pack persona" split already established for day/night). Full `vb_tests`
green (298/298) on `build-net-lua`; the actual rendered fog wasn't manually
eyeballed (no GUI in this agent environment). Full write-up under
REMAINING_TASKS.md's Phase 7.2 entry.

Before that, most recent landed item was **Phase 7.1: engine-side loading screen** — a new
`AppState::kLoading` in `src/client/main.cpp`, entered right after a
successful join instead of dropping straight into `kPlaying`, left once the
initial view-box of chunks has streamed in (`client->chunk_store().size()`
over an expected count mirroring `WorldReplicator`'s `chunks_in_view` box
shape, (2·view_distance+1)²·7) or an 8-second deadline elapses. Stage 1:
`MainMenu::draw_loading()` (`src/render/main_menu.cpp`) — a generic
`GuiProgressBar`, no data dependency. Stage 2: operator branding is text-only
(`S2CServerInfo::motd`, already replicated during the handshake) — no color
field was added (would need a protocol bump, left for a future pass, see
REMAINING_TASKS.md 7.1's own note). `kLoading` also drives the load itself
(pumps `sp->tick()`/`client->tick()` + `chunk_renderer->sync()` at a higher
budget than `kPlaying`'s steady-state 8/frame) rather than passively waiting.
Full `vb_tests` green (294/294) on `build-net-lua`; the windowed state
machine itself wasn't manually eyeballed this pass (no GUI in this agent
environment) — build + full test suite is the verification that exists.
Full REMAINING_TASKS.md write-up under Phase 7's 7.1 entry.

Before that, most recent landed item was **6.21: unified interaction reach + physics/action
read-back** — `net::ActionParams` (new struct, `inc/vb/net/
world_replicator.hpp`, sibling to `BlockEditHooks`) replaces both
`WorldReplicator`'s old hardcoded, non-overridable `kMaxReachBlocks` constant
and the separate `ServerSession::PunchParams::reach` with one shared value:
`WorldReplicator::set_reach()`/`reach()` hold it, and both `in_reach()`/
`apply_block_edit()` and `ServerSession::punch()` (via
`world_replicator()->reach()`, `src/net/session.cpp`) read it. Exposed to Lua
as `vb.action.set_params{reach=...}` / `vb.action.get_params()` (new
`vb["action"]` table, `src/script/pack_runtime.cpp`,
`PackRuntime::effective_action_params`, same "engine default, pre-freeze
override" shape as `vb.physics`/`vb.combat`) — a new namespace, deliberately
not folded into `vb.combat.set_params` (which keeps only `hit_radius`/
`player_damage`/`heal_after_seconds`/`heal_interval_seconds`), so a pack
author hunting for the mining-reach knob doesn't have to think to search
"combat" for it. Also added `vb.physics.get_params()` (effective
`physics::MoveParams` as a plain table). `content/base/mechanics.lua`'s
placing raycast now reads `vb.physics.get_params().eye_height`/
`vb.action.get_params().reach` instead of its own hardcoded `EYE_HEIGHT`/
`max_dist` constants, so a pack override can never silently desync Lua's
target-selection from the engine's own reach check. Wired into both
`src/server/main.cpp` and `src/client/main.cpp`'s `--singleplayer` path right
next to the existing `move_params`/`punch_params` wiring. New tests: a
`blockedit_test.cpp` case proves one `WorldReplicator::set_reach()` call
moves both the block-edit and `punch()` accept/reject boundary together; new
`pack_runtime_test.cpp` cases cover `vb.action.set_params` (override +
post-freeze rejection + built-in default) and
`vb.physics.get_params()`/`vb.action.get_params()` round-tripping the
effective values. Full `vb_tests` green (294/294) on `build-net-lua`;
`voxel_browser`/`voxel_browser_server` also rebuild clean.

Before that, most recent landed item was **Phase 5.3: keybindings screen** — a new
Settings -> Keybindings raygui screen (`MainMenu::open_keybindings`/
`draw_keybindings` in `src/render/main_menu.{hpp,cpp}`) lets a player click
an action's current key and press any physical key to rebind it, for the 6
`MovementBindings` axes (forward/back/left/right/jump/sprint; mouse
primary/secondary are left alone). Rebind capture is `GetKeyPressed()`
polled only while a row is "listening" (Esc cancels without changing it).
Persisted as 6 new `std::int32_t key_*` fields on `vb::core::ClientConfig`
(raw raylib `KEY_*` values, hardcoded as plain ints since `vb_core` doesn't
depend on raylib — see the field comments), read/written in
`src/core/config.cpp` alongside the existing fields, round-trip tested in
`tests/unit/config_test.cpp`. `src/client/main.cpp` builds its live
`MovementBindings` from config at startup and rebuilds it immediately on
Keybindings-screen Save (no restart needed, unlike window size/vsync).
Distinct from and composes with Phase 6.19 below (that's a pack-visible
*name* registry; this is which *physical key* produces the held/not-held
state Phase 6.19 exposes by name) — resolves the last open half of
REMAINING_TASKS' "Keybindings screen (5.3 Settings)" item. Full `vb_tests`
green (290/290) on `build-net-lua`.

Before that, most recent landed item was **6.19: engine-default keybind
pre-registration** — `PackRuntime`'s
constructor now seeds 8 fixed names (`move_forward`, `move_back`,
`move_left`, `move_right`, `jump`, `sprint`, `primary`, `secondary`) into
the same Phase 6.3 `keybind_names` registry `vb.register_keybind` writes
into, before any pack script runs. `src/client/main.cpp`'s
`sample_input_cmd()` now sets the matching `InputCmd::keybinds` bits (found
by name in `ClientSession::registered_keybinds()`) alongside the existing
`cmd.move`/`cmd.buttons` it already set from `MovementBindings` — purely
additive, no wire-format or physics change. This resolves REMAINING_TASKS'
open "do movement axes fit the boolean-keybind shape" question (yes —
keyboard/mouse input is already boolean) but does **not** add key
rebinding: `MovementBindings`' WASD/Space/Shift/mouse mapping is still
hardcoded client-side; a real rebind UI is still Phase 5.3's keybindings
screen, untouched by this. Bumped 2 tests that assumed a bare
`keybind_names`/`registered_keybinds()` size or index (`pack_runtime_test.cpp`'s
cap test, `pack_runtime_integration_test.cpp`'s "dash" test) to account for
the 8 pre-registered slots. Full `vb_tests` green (289/289) on
`build-net-lua`.

Before that, most recent landed item was **6.18: Growtopia-style discrete
punch combat** — attack is a single
"punch" per click (not hold-to-mine), resolved server-side via
`ServerSession::punch(NetId)` against a vertical-cylinder hit-test over
blocks and nearby players, exposed to Lua as `player:punch()`. Right after
it, **6.17 decoupled block breaking from the engine entirely** (the
client's old hardcoded hold-to-break timer is gone; a content pack now owns
break timing via `player:break_block()` + `vb.on("player_input", ...)`) and
movement bindings were pulled into a `MovementBindings` struct (still
continuous/server-authoritative, not migrated to the boolean keybind
registry). Full `vb_tests` green (289/289) on `build-net-lua`; all 4 CTest
cases pass. **Full writeups for 6.18 (+ its block-self-heal follow-up),
6.17, 6.15, 6.14, 6.13, and the Phase 1.3 networking-polish item live in
`state/changelog-recent.md`** — these were never copied into the older §8
log and are NOT covered by the two files below. Full writeups for 6.16 and
every earlier landed feature live in `state/changelog-part1.md` (Phase
6.1-6.16 + investigations + Phase 0-4 bootstrap) and
`state/changelog-part2.md` (Phase 5.x content/UI + Phase 6 design passes +
librg) — **read all three before assuming something is unimplemented or
before re-deriving a design decision already made.**

**Known regression, deliberately accepted:** `client.break_progress()`
always returns `nil` now (6.17 removed the client-local timer it read; no
server-authoritative replacement exists yet — `BlockDamageSystem`'s damage
*value* was never wired to the wire protocol, see `state/changelog-part1.md`
Phase 6.5's entry). `content/base/ui/hud.lua` silently draws no progress bar
until that lands. Traded deliberately per explicit user direction
("breaking is opt-in content").

**Known simplification, not attempted:** no engine-side punch-rate cooldown
(6.18) — a pack that doesn't edge-detect input could call `punch()` every
tick; left as the calling pack's responsibility, same posture as every
other "engine provides the primitive" seam in this codebase.

---

## 0. Repo snapshot

- `674d88f Initial commit` + the Phase 0 restructure (uncommitted at time of
  writing). Still **no tags**. Remote: `https://github.com/RechieKho/voxel_browser.git`.
- Source tree matches spec §4: `vb_core` (`src/core/`), `vb_render`
  (`src/render/`), `voxel_browser` (`src/client/`), `voxel_browser_server`
  (`src/server/`), `vb_tests` (`tests/`). Other module dirs are `.gitkeep` stubs.
- Builds green on Windows/clang with `-DVB_WARNINGS_AS_ERRORS=ON`.
- `ARCHITECTURE_SPEC.md` / `REMAINING_TASKS.md` are still design intent for
  Phase 1+.

---

## 1. Build-blocking bugs — ✅ all fixed in Phase 0

All four were fixed during the Phase 0 restructure (2026-09-10):
version info moved to a generated `vb/core/version.hpp`; raylib pinned to
`5.5` (not `6.0` — that tag never existed) with raygui matching `4.0`;
`cmake_minimum_required` bumped to `3.25`; a `CMAKE_POLICY_VERSION_MINIMUM
3.5` shim in `cmake/Dependencies.cmake` works around CMake ≥ 4.0 rejecting
doctest 2.4.11's `cmake_minimum_required(VERSION 3.0)` (remove the shim
once doctest ships a fix). Full detail in `state/changelog-part1.md`'s
2026-09-10 entries.

## 2. Naming — ✅ resolved

`PROJECT_NAME` is `voxel_browser`. Executables: `voxel_browser` (client),
`voxel_browser_server`. CI artifacts are
`voxel_browser-<target>-<arch>-<build_type>`.

---

## 3. CI landmines

- **No tags exist.** `setup_metadata.yml` runs `git describe --tags
  --abbrev=0` for the `version` output — this **errors** with no tags in
  history. `bundle`/`publish` depend on `setup_metadata`; first `git tag
  v0.0.1` will unblock. Until then anything past `build_*` is untested /
  likely red.
- `publish.yml` triggers only on `v*.*.*` tags and pulls artifacts from
  `runner.yml` by name `${project_name}` (note trailing space in the YAML
  on `name:` line 26 — `action-download-artifact` may or may not trim it).
- `lint.yml` installs clang-format via `pip install` then runs it via
  `pipx run clang-format` (dead weight from the pip install). Lints `src/**`
  with `--Werror` — every new file under `src/` must be clang-format-clean.
- Build workflows use `actions/checkout@v4 submodules: recursive`, but
  **there are no submodules** — deps are `FetchContent`. Harmless.
- `build_linux.yml` installs X11/GL dev packages for raylib, plus
  `libssl-dev libprotobuf-dev protobuf-compiler` for `VB_WITH_NET`.
- **`VB_WITH_NET` CI coverage is uneven:** Linux (apt) and Windows (vcpkg)
  build it; **macOS does not** (single-arch Homebrew protobuf vs. the
  universal arm64+x86_64 build — see `state/dependencies-detail` pointer
  below). If macOS ever gets it, `build_macos.yml`'s configure step is
  where to add it.
- **Never call `find_package(Protobuf REQUIRED)` a second time anywhere in
  the tree.** GameNetworkingSockets' own `src/CMakeLists.txt` already calls
  it; a second call (even as a "fail fast" convenience) fatal-errors on
  some protobuf installs (Homebrew, not vcpkg) with "Some (but not all)
  targets in this export set were already defined." Fixed 2026-09-11 by
  deleting the redundant call — don't reintroduce it.
- **Machine-local build/toolchain/agent-shell gotchas (vcvars64.bat path,
  Bash-vs-PowerShell-tool quirks, which pre-built `build-*` dirs actually
  exist, macOS-specific findings) live in `STATE.md.local`, not here** —
  gitignored (`*.local`), specific to whatever physical machine an agent
  session runs on; check it first when setting up a build in an agent
  shell, and add to it rather than here when you hit a new one.

---

## 4. Config / style quirks

- `.clang-format` is **Godot's** clang-format file verbatim. The C++ rules
  that matter: **tabs** (`UseTab: Always`, `TabWidth 4`), `ColumnLimit: 0`
  (no auto-wrap), `AccessModifierOffset: -4`, pointers right-aligned,
  `Cpp11BracedListStyle: false`. `Standard: c++17` but CMake sets
  `CMAKE_CXX_STANDARD 20` — fine unless C++20-only syntax confuses the
  formatter; bump to `c++20` if that happens.
- `.gitignore` ignores `build`, `*.local`, `compile_commands.json`,
  `.vscode/*` (except `extensions.json`). `CMAKE_EXPORT_COMPILE_COMMANDS
  ON` is set — symlink/copy `build/compile_commands.json` to root for
  tooling.
- `CMakeLists.txt:54` uses `file(GLOB_RECURSE SOURCE_FILES ...)` — adding a
  `.cpp` doesn't trigger reconfigure. Keep `cmake` re-runs in the loop.
- `libfantastic` (the lib target) is `add_library(... INTERFACE)` — assumes
  header-only. `vb_core` is a real STATIC lib; don't carry the INTERFACE
  assumption forward.
- `.gitignore`/`tests/CMakeLists.txt` are checked in with CRLF endings
  (every other text file is LF-only) despite `core.autocrlf=input` locally
  — `git add` warns harmlessly today, but the first real edit through a
  CRLF-preserving tool will silently flip the whole file's line endings,
  burying the actual diff. No `.gitattributes` forces this repo-wide. If
  ever cleaned up, use `git add --renormalize <path>` after adding a
  `.gitattributes` rule, not a manual find/replace.

**Longer-form gotchas (compiler/toolchain traps, full detail in
`state/gotchas.md`):**
- `std::erase`/`std::remove` on a `std::vector<ChunkCoord>` (or any small
  trivially-copyable struct that size/alignment shape) fails to compile
  under this repo's clang-targeting-MSVC-STL toolchain
  (`static_assert(false, "unexpected size")` inside `<xutility>`). Use a
  manual erase loop instead.
- A local `clang-format --dry-run --Werror` binary may not be trustworthy
  as-is against this repo's `.clang-format` — verify by diffing against an
  unmodified `HEAD` file before trusting either a pass or a wall of
  violations.
- `doctest`'s `--test-case=` filter is a **glob pattern**, not a substring
  match — wrap in `*...*` and quote it (zsh glob-expands an unquoted
  pattern itself).
- MSVC (`cl.exe`) rejects a ternary between two different instantiations of
  a templated smart-pointer type with converting constructors (`C2445`) —
  use plain `if`/`else` instead. Clang/GCC not cross-checked.
- A sibling file's "isn't implemented yet" comment can go stale the moment
  a later phase lands it, without the comment ever being updated — grep the
  actual current binding (e.g. `src/script/pack_runtime.cpp`) before
  reusing a sibling's "this doesn't work yet" framing in new code.
- A client built without `VB_WITH_COMPRESSION` can never asset-sync
  against a server built with it on (`hash_bytes()` stubs to all-zero) —
  every real (non-`--singleplayer`) connection needs **matching**
  `VB_WITH_COMPRESSION` on both binaries; there's no runtime negotiation.
- A second, independent bug produces the identical `"asset transfer failed
  (hash mismatch or size cap)"` text even with matching compression flags:
  the server used to build the asset manifest *before* `vb.storage`'s
  deferred first-tick flush reached disk. Fixed by calling
  `pack_runtime.flush_storage()` before `build_manifest()` in
  `src/server/main.cpp`. **This error string has (at least) two unrelated
  root causes** — check both before assuming which one you've hit.

---

## 5. Dependency notes / unknowns

- **Cellulose** (`github.com/RechieKho/cellulose`) — evaluated as a greedy
  mesher, then reverted after it reproduced an NVIDIA-driver VAO/VBO
  heap-corruption crash (greedy-merged geometry blows past the renderer's
  buffer-reuse headroom far more often than the hand-rolled per-face
  mesher). Hand-rolled `vb::world::chunk_mesher` is the **permanent**
  meshing backend now, not a placeholder. `VB_WITH_MESHING` scaffolding was
  later removed outright at user request (2026-09-17). Full story in
  `state/changelog-part2.md`.
- **FastNoise2**: real upstream is `Auburn/FastNoise2` (the README's
  `electronicarts/fastnoise` link is a fork, don't use it). Pin is
  `v0.10.0-alpha`, not `v0.10.0` (that tag doesn't exist — a real,
  previously-unnoticed bug fixed in Phase 6.14, see `state/changelog-part1.md`).
- **GameNetworkingSockets** — resolved (Phase 1.2, 2026-09-11), the single
  hardest dependency in the project. Protobuf cannot be `FetchContent`-ed
  (must be a real package-manager install: vcpkg/apt/brew). GNS pinned to
  `v1.6.0` (not `v1.4.1`, which doesn't compile under a modern stdlib). ICE/
  WebRTC disabled. Windows crypto is BCrypt; Linux/macOS use system
  OpenSSL. Linked statically. **Full story, including local-verification
  toolchain quirks and the Homebrew double-`find_package` crash, is in
  `state/changelog-part1.md`'s Phase 1.2 entries — read those before
  touching `cmake/Dependencies.cmake`'s GNS block again.**
- **librg** is `zpl-c/librg`, pinned `v7.4.0`, wired in as the default
  entity-replication backend since 2026-09-17 (`VB_WITH_REPLICATION`
  default ON; the original hand-rolled linear scan remains as an explicit
  opt-out). Known limitation: chunk ids need `chunkamount.x*y*z` to fit
  signed int32 and each axis casts to `int16_t` — picked 1024 chunks/axis
  (±512×cell_size world units); an entity straying outside that range is
  silently excluded from interest until it re-enters. Full detail in
  `state/changelog-part2.md`.
- **Lua**: PUC-Lua has no upstream CMake; needs a wrapper `CMakeLists.txt`.
  sol2 is the chosen binding layer (pinned `v3.5.0`, not `v3.3.0` — that
  version's bundled optional impl fails under Clang ≥ 18).
- **raygui** is header-only (`raysan5/raygui`), needs exactly one TU with
  `#define RAYGUI_IMPLEMENTATION` in its own target (`vb_raygui_impl`) so
  project `-Werror` never touches vendored code — same pattern used for
  librg's `LIBRG_IMPL`.

---

## 6. Design decisions still open

Tracked in `ARCHITECTURE_SPEC.md` §18, repeated here for visibility:

1. ~~Lua binding layer: `sol2` vs. raw C API.~~ **Resolved: sol2.**
2. ~~Cellulose meshing API shape.~~ **Resolved and reverted** (2026-09-16)
   — hand-rolled meshing stays permanent.
3. librg version + whether we use its serialization or only its interest
   culling. **Resolved (2026-09-17): interest culling only**, own codec for
   payloads.
4. Chunk compression: LZ4 (spec's starting choice) vs. zstd vs.
   palette-only. Still open.
5. ~~World persistence / region file format.~~ **Resolved 2026-09-25:** flat
   per-region files (`vb::world::RegionStore`), not the previously-noted LMDB
   direction — reverted after review, see "Current status" above. Wired into
   `voxel_browser_server` only; `--singleplayer` still has no persistence.
6. Auth: `auth_mode = none | token` — handshake reserves the field, no
   service exists.

Other undecided:
- Test framework: doctest (in use) vs. Catch2 — never revisited since
  Phase 0's pick.
- Whether the client embeds the server for singleplayer as a library or
  spawns a child process — **currently: in-process library**
  (`Singleplayer` in `src/client/main.cpp` constructs `LoopbackNetwork`/
  `ServerSession`/`ClientSession` directly).
- **`WorldReplicator` streams a whole player's view box in one uncapped
  burst per connect**, no per-tick pacing. GNS's send buffer was raised to
  32 MiB (2026-09-15) which comfortably covers the shipped default view
  distance (8/3, ~2023 chunks, ~545 KB measured) but doesn't add real
  backpressure — a larger view distance, denser world, or several players
  joining at once could still overflow it. Proper fix: a per-connection
  byte-budget-per-tick on the `diff.entered` send loop. **Revisit before
  ever raising the shipped default view distance.** Still open as of the
  2026-09-25 disk-load-budget fix above — that fix bounds
  `ChunkLifecycleSystem::update()`'s *ingest* side (worldgen + region-store
  disk loads) per tick, not this *send* side; a large view box still leaves
  in one uncapped burst once ingested.

---

## 7. Conventions to follow

- Namespaces `vb::<module>` (`vb::net`, `vb::world`, ...); headers under
  `inc/vb/<module>/`, mirrored by `src/<module>/`.
- No exceptions on hot paths; `Result<T,E>` for fallible ops. Never call
  `.error()` when a `Result` holds a value (union UB) — `REQUIRE(result)`
  then deref in tests, not `REQUIRE_MESSAGE(result, msg(err))`.
- Wire structs live in `inc/vb/protocol/`; every one gets a round-trip
  test and bumps `ENGINE_PROTOCOL_VERSION` + `docs/protocol.md` when
  changed. Concrete step-by-step for adding one: struct → `MessageType` →
  round-trip test → version bump → `docs/protocol.md` entry → integration
  test (see `CONTRIBUTING.md`).
- Keep both binaries buildable/runnable with `--headless` (CI +
  integration tests depend on it).
- Match `.clang-format`: tabs, no column limit, run clang-format before
  commit (CI `--Werror` on `src/**`).
- New Lua bindings should be **generic primitives, not game-specific
  ones** — game rules belong in `content/*` packs (e.g. `player:take()` is
  a generic inventory primitive; the actual crafting recipe list lives
  entirely in `content/base/crafting.lua`, not in engine code). See
  `CONTRIBUTING.md` for the worked example.
- Heavy deps (GNS, librg, Lua/sol2, FastNoise2, LZ4/xxHash) are declared in
  `Dependencies.cmake` but gated behind `VB_WITH_*` (several now default
  ON — check `CMakeLists.txt` for current defaults, don't assume OFF).
- doctest + MSVC STL: comparing/streaming a `std::string_view` in a
  `CHECK` needs `#include <ostream>` in that test TU (instantiates
  `toString<string_view>`).
- GCC gotcha: `uint64_t` (`unsigned long` on LP64) vs. `...ULL` literals
  trips `-Wsign-conversion` — always name wide constants `constexpr
  std::uint64_t`.
- Don't store `const BlockRegistry&` — callers pass `BlockRegistry::base()`
  temporaries; hold it by value.
- **xxHash/lz4 link order matters**: lz4's vendored source ships its own
  old `lib/xxhash.h` with no `XXH3_128bits`. `VB_WITH_COMPRESSION` links
  `xxHash::xxhash` **before** `LZ4::lz4` on purpose so the real header
  wins `-I` search order — don't reorder this.
- `std::vector<std::byte>` can't be built directly from
  `std::istreambuf_iterator<char>` — use the `read_whole_file()` helper in
  `assetsync/cache.cpp` instead of re-deriving it.

---

## Detail files

Every file below is full-detail material moved out of this core. Newest
work is summarized above under "Current status"; everything before that is
here, organized as it was originally written (mostly chronological,
newest-first within each file):

- **`state/changelog-recent.md`** — full writeups for the block-self-heal
  follow-up, Phase 6.18 (discrete punch combat), Phase 6.17 (decoupled
  block breaking + `MovementBindings`), Phase 1.3 networking polish
  (two-process smoke test, per-IP connection cap, hostname resolution),
  Phase 6.15 (kitchen-sink example pack), Phase 6.14 (Lua-driven worldgen/
  FastNoise2), Phase 6.13 (read-only server config visibility) — these
  phases were never logged into the older §8 structure below and are not
  duplicated in the next two files.
- **`state/changelog-part1.md`** — Phase 6.1/6.4/6.5/6.6/6.7/6.8/6.9/6.11/
  6.16 landed-feature writeups; the 2026-09-15 investigations (NVIDIA
  driver VAO/VBO heap corruption, FPS dip during chunk streaming,
  cross-chunk sky-light band, near-black flash on predicted breaks, chunk
  meshing moved off the main thread, GNS send-buffer overflow root cause);
  the 2026-09-10/11 Phase 0-4 bootstrap entries (config, transport,
  handshake, session layer, physics/netcode, worldgen, entity visuals,
  block editing, Lua VM); "Gotchas learned this pass".
- **`state/changelog-part2.md`** — Phase 5.1 `content/base` pack + the gap
  it found (neither binary loaded a pack at all before this); Phase 5.2-5.5
  (main menu, chat, player list/join-leave, day/night, death/respawn, sfx
  docs, real inventory sync + hotbar, hold-to-break progress, dropped-item
  entity, crafting recipes, `--singleplayer` running the real content pack,
  documentation pass); the Cellulose meshing spike and revert; librg wired
  in as the interest backend and flipped to default; Phase 6 design-only
  passes (entity classes, UI immediate-mode, keybind channel, `vb.db`,
  block-damage breaking, worldgen biome-selection redesign).
- **`state/gotchas.md`** — long-form compiler/toolchain traps referenced
  from §4 above (the `std::erase` MSVC-STL bug, the MSVC ternary
  ambiguity, the two independent asset-sync hash-mismatch bugs, the
  stale-comment lesson, the clang-format-untrustworthy-locally note).

Anything **not** listed above and not inline in this file no longer exists
in `STATE.md`'s history — if you're looking for something and can't find
it here, check `git log -- STATE.md` for when it might have been removed,
or `REMAINING_TASKS.md`/`ARCHITECTURE_SPEC.md` for design-level context.
