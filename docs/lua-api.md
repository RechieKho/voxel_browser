# Voxel Browser — Lua Content API Reference

> **Status: implemented and demonstrated.** The server content API
> (`ARCHITECTURE_SPEC.md` §10.3) and the client UI API (§10.4) are both real,
> and `content/base` (`content/base/`) is the worked, runnable example this
> doc keeps referring back to — it's the pack `voxel_browser_server` loads by
> default (`server.toml`'s `content_pack = "content/base"`), not sample code
> that lives only here. A few registration calls remain write-only until a
> later phase gives them a consumer (`vb.register_biome`, and parts of
> `vb.register_entity`) — each is called out below, in place, rather than
> implied by a blanket disclaimer at the top of this file.

## Binding layer

`sol2` (v3.5.0), header-only, over PUC-Lua 5.4 — decision recorded in
`ARCHITECTURE_SPEC.md` §18 Q1. (v3.3.0's bundled optional does not compile under
Clang ≥ 18.)

## Runtime — `vb::script::Vm` (`inc/vb/script/vm.hpp`, implemented, `VB_WITH_LUA`)

pImpl wrapper around one `sol::state`; the rest of the engine never includes
sol2, and a build without `VB_WITH_LUA` links a stub whose every call returns
`core::ScriptError::kDisabled`.

- `Vm(VmLimits{ memory_bytes, instruction_budget })`
- `ScriptResult do_string(code, chunk_name)` — compiles **source only**
  (`sol::load_mode::text`; bytecode is rejected) and runs it under the per-call
  hook. `ScriptResult` carries `{ok, core::ScriptError, message}` where `message`
  is the Lua error text + `debug.traceback`.
- `begin_call_budget()` — re-arms the instruction counter; the tick loop calls it
  before each pack callback (Phase 4.2).
- `memory_used()` / `memory_limit()` / `sandbox_intact()`.

Error classes: `kSyntax`, `kRuntime`, `kBudgetExceeded` (hook fired),
`kOutOfMemory` (allocator ceiling — recoverable, the VM stays usable).

## Server API (pack VM) — `vb::script::PackRuntime` (`inc/vb/script/pack_runtime.hpp`, implemented, `VB_WITH_LUA`)

pImpl'd the same way as `Vm` (a disabled build links a no-op stub); owns one
`Vm`, the content registries, the event bus, and `vb.after`/`vb.every`
timers. Construction is split because a `player_join` veto must wrap
`HandshakeServerHost::authenticate` **before** the `ServerSession` that owns
it exists:

```cpp
PackRuntime rt(transport, registry, storage_path);
rt.load_pack_file(pack_source);
rt.freeze();
rt.install_join_veto(host);           // before constructing ServerSession
ServerSession session(transport, cfg, host);
// ... construct WorldReplicator ...
rt.attach_world(*session.world_replicator());
rt.attach_session(session);
// main loop, after session.tick(dt):
for (auto &j : session.take_joins())  rt.dispatch_player_join_completed(j);
for (auto &l : session.take_leaves()) rt.dispatch_player_leave(l);
rt.dispatch_tick(dt);
```

- Registration (pack load only, rejected once `freeze()` has run):
  `vb.register_block(def) -> BlockId` (idempotent by `name`; **the real
  texture/atlas system**: `texture` is a pack-relative path (e.g.
  `"textures/stone.png"`), synced to the client over the existing Asset Sync
  virtual FS and packed into one atlas texture per session
  (`vb::render::TextureAtlas`) — a block with no `texture` (the default,
  empty string) keeps rendering a flat placeholder color. `model` is still
  accepted but not stored — no wire-visible model field exists on
  `BlockType` yet; `max_damage` — Phase 6.5, default `0` = today's instant
  break — opts the block into the shared block-damage breaking system below,
  replicated to clients as part of `S2C_BlockRegistry`. **Idempotent
  registration doesn't update an already-registered block's other
  properties** — re-registering an existing `name` (e.g. one of the base 8)
  just returns its id unchanged; `max_damage`/`solid`/`opaque`/etc. only take
  effect the first time a name is registered. `texture` is the one
  exception: re-registering an existing `name` with a non-empty `texture`
  still attaches it (`BlockRegistry::set_texture`) — this is how
  `content/base/blocks/stone.lua`/`water.lua` give an already-hardcoded
  `BlockRegistry::base()` block a real texture without needing to touch
  `src/world/block.cpp`; `region` — Phase 7.3, generic per-tick
  occupancy tracking opt-in, independent of `liquid` (see `region_enter`/
  `region_exit` below) — defaults to `liquid`'s own value (a liquid block
  opts in automatically, matching `base:water`) unless given explicitly,
  `vb.register_item(def)`, `vb.register_entity(def)`
  (`name` (idempotent-by-name), `on_spawn`/`on_tick`/`on_hit`/`on_death`
  callbacks, opt-in `health=` (`entity:get_health()`/`set_health()`,
  auto-despawn at 0), `width=`/`height=` billboard footprint (default
  `0.8`/`1.8`, the placeholder's own dimensions) — real dispatch is a small
  hardcoded system (`PackRuntime::Impl::entities`), not an EnTT registry
  (Phase 3.1 still doesn't exist server-side for this). `visual = {...}`
  (entity-management follow-up, schema finalized 2026-09-17 in
  `architecture_spec/rendering.md` §11.3) is now real: `variant` (one of the
  9 named frame-size presets, `small` through `large_flat` — see the table
  in `rendering.md`), `texture` (pack-relative path, synced like any asset),
  `facings` (4 or 8, default 8), `origin = {x=, y=}` (normalized anchor
  within a frame, default `{0.5, 1.0}` = bottom-centre feet point, each
  component in `[0, 1]`), and `clips` (a non-empty array of
  `{clip=, frames=, fps=}`, `frames`/`fps` both positive) — all validated at
  registration (`PackRuntime`'s `parse_entity_visual`); the sheet's real
  pixel dimensions are validated separately, client-side, once the texture is
  actually decoded (`render::build_entity_visual_layout`) — a mismatch there
  logs a warning and that kind keeps the flat `width`/`height` placeholder
  rather than failing pack load. A kind that never sets `visual` is
  unaffected either way. Per-instance override
  (`ScriptState.visual_override`, e.g. skins) is still a real, separate,
  not-yet-implemented follow-up. `vb.register_biome(def)` (Phase 6.14: `name`
  (idempotent-by-name, mirrors every other registration function),
  `surface`/`filler`/`stone` (block *names*, resolved to `BlockId`s via the
  registry when a pipeline is compiled), `probability` (base Voronoi-cell
  draw weight, default `1.0`), `adjacency = {["other:biome"] = weight, ...}`
  (soft multiplier vs. an already-resolved neighbor cell, default `1.0` for
  an unlisted pair, engine-floor-clamped so it's never a hard exclusion —
  see `vb.worldgen.set_pipeline` below), and `decoration` — a *table* of
  `{spawn_rate=, blocks={{x=,y=,z=,block=},...}}` schematic entries is a real
  consumer (Phase 6.14); a plain *string* (`content/base`'s pre-6.14 usage,
  e.g. `decoration = "trees"`) stays captured-but-inert, unchanged behavior.
  Every `vb.register_biome` call becomes one Voronoi-cell candidate only once
  a pack also calls `vb.worldgen.set_pipeline` — with no such call, biomes
  stay captured exactly like before this phase, since `WorldGenerator` keeps
  running its fixed single-biome-shaped default. / `vb.register_craft(def)` (captured;
  the engine itself doesn't read it back, but `content/base/crafting.lua`
  is a real, working example built on top of it — see below).
  `vb.register_keybind(name) -> index` (Phase 6.3, idempotent by `name`)
  declares a closed-schema custom input slot — `index` (registration order)
  becomes bit *index* of every `InputCmd.keybinds` going forward, capped at
  32 registrations (`S2CKeybindRegistry::kMaxKeybinds`) so the bitset always
  fits one `u32`; an unregistered key literally cannot be represented on the
  wire. Reaches joining clients as `S2C_KeybindRegistry` when
  `PackRuntime::install_keybind_registry(host)` is called (`src/server/
  main.cpp` does, right alongside `install_join_veto`), same opt-in shape as
  `block_registry`. Phase 6.19: every `PackRuntime` pre-registers 8 engine
  names — `move_forward`, `move_back`, `move_left`, `move_right`, `jump`,
  `sprint`, `primary`, `secondary` — before any pack script runs, so they're
  always present in `S2C_KeybindRegistry` and readable via
  `input.keybinds["jump"]` etc. just like a custom keybind. This is purely
  additive: `input.buttons`/`input.move` (and the client's hardcoded WASD/
  Space/Shift/mouse physical keys) are unchanged — it just makes these
  actions *discoverable* the same way a custom keybind is, for a future
  rebind-UI to enumerate. Calling `vb.register_keybind` with one of these
  names returns the same pre-assigned index (idempotent-by-name already
  covers it); it does not let a pack change which physical key drives it.
  `vb.worldgen.set_pipeline{height=, base_height=, amplitude=, sea_level=,
  soil_depth=, cell_size=, carvers={{noise=, threshold=, y_min=, y_max=},
  ...}, veins={{block=, target_rock=, height_min=, height_max=, vein_size=,
  spawn_rate=}, ...}}` (Phase 6.14, pack-load-time only) replaces
  `WorldGenerator`'s fixed fBm-heightmap default with a pack-driven pipeline
  — `height` (required, a `vb.noise.*`-built node) plus every registered
  biome are compiled *once*, on the main thread, into an immutable
  `worldgen::PackWorldGenPipeline` (`PackRuntime::build_worldgen_pipeline`,
  called right after `freeze()`, before constructing `WorldGenerator`/
  `WorldGenWorkerPool` — same call-site shape as `effective_move_params`).
  **Deliberate deviation from this item's original `set_pipeline(fn)`
  phrasing:** Lua/sol2 is strictly single-threaded and
  `WorldGenWorkerPool` calls `WorldGenerator::generate()` from N worker
  threads with no locking, so the pipeline can't literally be "a Lua
  callback run per chunk" — it's data, compiled once into plain C++ (and,
  when `VB_WITH_WORLDGEN` links real FastNoise2, into an actual FastNoise2
  `SmartNode` graph — see `vb/worldgen/fastnoise2_compile.hpp`; the
  dependency-free `vb/core/noise.hpp` evaluator is what runs otherwise, same
  "zero-dependency default, optional backend on top" posture as every other
  `VB_WITH_*` flag). No call at all (the common case) leaves
  `WorldGenerator` on its exact pre-6.14 fixed path — byte-identical output,
  `tests/unit/worldgen_test.cpp`'s golden-hash gate for the default path is
  unaffected.
  `vb.noise.*` builds the node-graph description `set_pipeline`'s `height`
  and each carver's `noise` field expect — plain tagged Lua tables, not
  opaque handles: `vb.noise.constant(v)`, `vb.noise.value{frequency=}`,
  `vb.noise.cellular{frequency=}`, `vb.noise.fbm{source=, octaves=,
  lacunarity=, gain=, frequency=}`, `vb.noise.remap{source=, in_min=,
  in_max=, out_min=, out_max=}`, `vb.noise.combine{a=, b=,
  op="add"|"multiply"|"min"|"max"}`.
  **Decoration is schematic-only, not procedural** (explicit scope
  narrowing, same threading reasoning as above): a per-site Lua callback
  can't run on a worker thread either, so `vb.register_biome`'s `decoration`
  table is a pure-data offset list, not a callback — see that entry above
  and `REMAINING_TASKS.md`'s Deferred section for what's intentionally not
  attempted here (cross-chunk decoration, procedural/callback schematics,
  Voronoi cell resolution result caching).
  A pack registering blocks beyond the Phase 2 `base()` set logs an info
  line; those ids reach the client if (and only if) the host wires
  `HandshakeServerHost::block_registry` from this same registry
  (§4.3/`S2C_BlockRegistry` — `src/server/main.cpp` does). The server-side
  registry starts from `base()` so the well-known 8 ids still match the
  client's fallback `base()` call when no registry frame is sent at all.
- Runtime world (needs `attach_world()`): `vb.world.get_block(x,y,z)`,
  `vb.world.set_block(x,y,z,id)` (**known gap:** doesn't run the relight
  cascade `C2S_BlockEdit` does — can desync lighting until something else
  touches the chunk), `vb.world.raycast(origin, dir, max) -> {hit,x,y,z,nx,ny,nz}|nil`,
  `vb.world.spawn(kind, pos)` (logs and returns `nil` — no entity system to
  spawn into yet, Phase 3.1). `vb.world.spawn_item_drop(pos, item, count)`
  (needs `attach_session()`, not just `attach_world()`) is a separate,
  hardcoded-but-real path: spawns a dropped-item entity backed by
  `vb::world::ItemDropSystem` (`src/net/session.cpp`), replicated through the
  same interest-grid/`S2C_EntitySnapshot` machinery a player uses, no new
  wire message — a nearby player auto-collects it into their inventory. Not
  routed through `vb.register_entity`/`vb.world.spawn` at all; see
  `content/base/blocks/*.lua`'s `on_break` handlers for the intended usage.
  Phase 6.11: `vb.register_block{pickup_radius=..., item_lifetime_seconds=...}`
  overrides `ItemDropSystem`'s own construction-time defaults (1.5 blocks /
  120s) for drops of that specific block — e.g. a magnet-radius power-up or a
  rare item that never despawns. Unset (or negative) means "use the engine
  default"; same idempotent-registration caveat as `max_damage`/`max_stack`
  applies — re-registering an existing block name doesn't update it.
- Entity / player Lua object (needs `attach_session()`; one merged usertype
  today since no non-player entity exists): `:get_pos() -> {x,y,z}`,
  `:set_velocity(x,y,z)`, `:remove()` (no-op, logged — nothing to remove
  from), `:get_inventory() -> {{item,count}, ...}`, `:give({item,count})`,
  `:take({item,count}) -> bool` (removes up to `count` of `item` across
  however many slots hold it; `false` and no change at all if the player
  doesn't have enough — all-or-nothing, not partial), `:send_message(text)`,
  `:open_ui(name, ctx?)`, `:get_name()`, `:damage(amount, cause?)` (Phase
  6.6 — the one way to reduce a player's health from Lua; `cause` is an
  opaque string, e.g. `"fall"`/`"pvp"`, threaded through unchanged to a
  `player_death` handler below).
  `send_message`/`open_ui` are real, framed messages (`S2C_Chat`/`S2C_OpenUi`,
  see `docs/protocol.md`) that a real client actually handles: `send_message`
  lands in the HUD chat log (`src/client/main.cpp`, Phase 5.4) exactly like a
  normal chat line, and `open_ui` is drawn by `UiRenderer` (Phase 4.5/5.1).
  **`vb.register_item` never allocates its own id space** — a held item and a
  placeable block still share one `BlockId`, so every `item` field above
  (`give`/`take`/`get_inventory`) is really a `BlockId`; anything a pack
  wants a player to be able to hold has to go through `vb.register_block`
  today, crafted-only materials included (see `content/base/blocks/
  planks.lua`/`sticks.lua`).
- Events: `vb.on("player_join"|"player_leave"|"block_break"|"block_place"|
  "player_interact"|"chat"|"tick"|"ui_event"|"player_death"|"player_input"|
  "block_break_begin"|"block_break_tick"|"block_health_tick"|
  "region_enter"|"region_exit", handler)`,
  vetoable via `return false` (except `tick`/`ui_event`, which have
  no veto semantics; `player_death` is a *decision* hook, not a veto —
  see below; `player_input`/`chat` may veto *or* replace, see below;
  `block_break_tick`/`block_health_tick` return numbers, not booleans —
  see below; `region_enter`/`region_exit` are pure notifications, any
  return value is ignored, see below).
  `player_join` fires from `install_join_veto`'s `authenticate` wrapper (a
  real pre-join veto — note it hands the handler a plain player *name*
  string, not a `Player` handle, since no session/connection exists yet at
  that point). `block_break`/`block_place` fire from `WorldReplicator::
  apply_block_edit`'s `BlockEditHooks` seam with a real `Player` handle +
  position, and a block's own `on_break`/`on_place` callback (from
  `register_block`) fires separately, after the edit is applied. `chat`
  fires from a real `C2S_Chat` (Phase 5.4) as `function(player, text) ->
  boolean|string?` before `ServerSession` broadcasts `S2C_Chat`: `return
  false` vetoes the line outright, `return "new text"` (Phase 6.10) rewrites
  it for the next handler in registration order and, if it's the last one to
  touch it, for the broadcast itself, and `true`/`nil`/anything else passes
  the current text through unchanged — the same veto-or-replace chaining
  shape as `player_input` below, just for one string field instead of a
  table. `content/base/crafting.lua` is the reference example of building a
  whole feature (recipe parsing, ingredient checks) entirely on top of this
  one event (veto-only, no text rewriting). `ui_event` fires from `C2S_UiEvent`
  (Phase 4.5). `player_interact` is still wired generically
  (`PackRuntime::dispatch_player_interact`) with nothing calling it — no
  interact wire message exists yet. `player_death` (Phase 6.6) fires once
  per respawn — health reaching 0, for any cause, void-kill included — as
  `function(player, cause, health_before) -> table?`; the *first* registered
  handler that returns a table wins (not a veto chain), and that table may
  set `heal` (new health), `pos` (`{x,y,z}`, the new position), `message`
  (a private chat line, `""`/omitted = none), and `drop_inventory` (spawns
  every inventory slot as a real dropped-item entity **at the player's
  death position**, not wherever they're about to respawn, and empties
  their inventory). No handler registered at all (or none returns a table)
  falls back to the engine's built-in behavior — full heal, teleport to the
  join spawn point, `"* you died and respawned"` — unchanged from every
  pre-6.6 build. `player:get_pos()` inside the handler still reads the
  death position (the engine hasn't moved the player yet at this point),
  useful for anything beyond the built-in `drop_inventory` flag.
  `ServerSession::spawn_point(net_id)` (not a Lua binding, a host-side
  accessor) exposes the original join spawn point if a handler wants to
  fall back to it deliberately.
  `player_input` (Phase 6.3) fires once per `InputCmd`, before movement
  integration, as `function(player, input) -> false | table | nil`. `input`
  is `{ move = {x,y,z}, yaw, pitch, buttons = {jump,sprint,primary,
  secondary,fly_up,fly_down}, keybinds = {[name] = bool, ...} }` (only
  *registered* keybind names ever appear as `keybinds` keys). Returning
  `false` vetoes — the cmd's effect on movement/rotation is dropped entirely
  for that tick (its seq is still consumed/acked, so the client doesn't
  replay it forever); returning a table overrides only the fields present in
  it (omitted fields keep the previous value) — e.g. `return { move = {
  z = 2.0 } }` to reinterpret input as a dash. Multiple handlers chain in
  registration order, each seeing the prior one's output; the first `false`
  short-circuits the rest. Movement/look fields (`PlayerInput`'s existing
  wire shape) are otherwise untouched by this channel — it's additive.
  Only installed (`ServerSession::set_input_handler`) when a pack actually
  registers a `player_input` handler, so packs that don't use it pay zero
  extra cost in the per-tick input path.
  Shared block-damage breaking (Phase 6.5, spec §10.7) — only for a block
  with `max_damage > 0` (§`register_block` above); a `max_damage == 0` target
  never reaches any of these three, staying today's instant break.
  `C2S_BlockBreakBegin{pos, face}`/`C2S_BlockBreakStop{pos}` bracket a client
  holding on a target (`ClientSession::send_block_break_begin`/
  `send_block_break_stop` — no base-pack/client UI wires these yet, same
  "mechanism before content" posture as every other Phase 6 primitive).
  `block_break_begin(player, pos) -> bool?` gates entry — vetoable, on top of
  the engine's own reach + `max_damage > 0` checks. `block_break_tick(player,
  pos, max_damage) -> number` fires once per tick per contributing player
  (multiple concurrent players "breaking together" sum their own return
  values; multiple registered handlers for the same call also sum, an
  orthogonal case). `block_health_tick(pos, damage, max_damage,
  ticks_since_last_hit) -> number?` fires once per tick per currently-damaged
  block regardless of contributors — return a replacement damage value, or
  nothing for "unchanged" (the last handler to return a number wins if
  several are registered). No handler for either tick event = zero built-in
  policy: `block_break_tick` absent means damage never accrues at all;
  `block_health_tick` absent means permanent damage, no healing. Completion
  (summed damage reaching `max_damage`) drives the *existing*, unchanged
  `C2S_BlockEdit`/`block_break`/`on_break` pipeline — this system only gates
  *when* that fires. No wire message replicates the damage value itself to
  nearby players yet (blocked on the still-pending texture/atlas system for
  the crack overlay, `REMAINING_TASKS.md` 6.5), so a second player can't see
  another's break progress today, only feel its effect once it commits.
  Generic region occupancy (Phase 7.3): `region_enter(player, pos,
  block_name)` / `region_exit(player, pos, block_name)` fire once per
  crossing (not once per tick spent inside), server-side, whenever a
  player's own position moves into/out of a block whose `BlockType.region`
  flag is set (`register_block{region=...}` above) — `pos` is the voxel
  they crossed into/out of, `block_name` its registered name (e.g.
  `"base:water"`, the only block that opts in by default). Purely a
  notification, no veto — a pack builds its own policy on top (e.g. slow
  movement in water) from this plus `player:set_velocity`/its own state, the
  engine hardcodes no swim-speed or liquid-specific behavior at all. Water
  is just the first user of a generic mechanism; a future lava/gas/
  poison-cloud block reuses it with zero engine changes. Only installed
  (`ServerSession::set_region_hooks`) when a pack registers at least one of
  the two events, same zero-extra-per-tick-cost posture as the block-damage
  hooks above.
- Scheduling: `vb.after(seconds, fn)` (one-shot), `vb.every(seconds, fn)`
  (repeating; catches up on a stalled tick, capped at 8 fires/dispatch).
  Storage: `vb.storage.key = value` — a metatable-backed proxy over a
  `nlohmann::json` object, persisted to `<content_pack>/storage.json`
  (flushed once per tick when dirty, and in `flush_storage()`).
- Generic per-key storage (Phase 6.4): `vb.db.get(key)` / `vb.db.set(key,
  value)` / `vb.db.delete(key)` — distinct from `vb.storage` above, `key` is
  whatever the script chooses (`"user:" .. name`, `"session:" .. token`, ...)
  and `value` round-trips through the same JSON conversion as `vb.storage`
  (tables/numbers/strings/booleans, not just strings). Written immediately
  (no dirty-flag/flush step, unlike `vb.storage`). Backed by
  `vb::script::ScriptDb` (`inc/vb/script/db.hpp`): each key's file lives at
  `<content_pack>/db/<sha256(key) 2-hex-prefix>/<sha256(key)>`, the same
  content-addressed shard layout as `vb::assetsync::ClientAssetCache`. No
  enumeration API — get/set/delete by an already-known key only. The engine
  has no notion of "logged in": a connection stays just a connection until a
  pack's own login flow (built on `vb.db`) looks up a record and decides to
  recognize it. `vb.crypto.hash(data)` — SHA-256 hex digest
  (`vb::core::sha256_hex`, `inc/vb/core/sha256.hpp`) — so a pack implementing
  its own login doesn't have to roll credential hashing in pure Lua (the
  sandbox strips `os`/`io`, §10.2). The engine still takes no position on
  auth as a concept (spec §18 Q6).
- Physics tunables (Phase 6.7): `vb.physics.set_params{gravity=..,
  walk_speed=.., sprint_speed=.., jump_speed=.., accel=.., air_accel=..,
  friction=.., step_height=.., fly_speed=.., fly=.., half_width=..,
  height=.., eye_height=.., terminal_velocity=..}` overrides
  `physics::MoveParams` (pack-load time only, rejected once frozen) — only
  the fields the table sets are replaced; everything else keeps the
  operator's `server.toml` default (`gravity` there is the base a pack
  override wins over, not the other way round). One global override, not
  per-entity-kind — no entity kind besides the player runs physics today.
  Reaches joining clients as `S2C_MoveParams` so client-side prediction uses
  the exact same tunables as the server's authoritative simulation, same
  opt-in-hook shape as `block_registry`/`keybind_registry`.
- Day/night curve (Phase 6.8): `vb.daynight.set_curve{keyframes = {{tick=,
  brightness=, color={r,g,b}}, ...}}` overrides the engine's default
  4-keyframe sky gradient (`vb::world::default_day_night_curve()`); rejects
  an empty/missing `keyframes` table and any call after `freeze()`. Reaches
  joining clients as `S2C_DayNightCurve`, same "no call, no frame, client
  keeps the default" opt-in shape as `vb.physics.set_params`/`S2C_MoveParams`
  above — the client actually renders with the overridden curve
  (`ClientSession::day_night_curve()`), not just server-side bookkeeping.
  `vb.daynight.set_day_length(seconds)` (rejects `seconds <= 0`) overrides
  the real seconds one in-game day takes, the same config-then-pack-override
  shape as `gravity` (`server.toml`'s `day_length_seconds` is the base a pack
  override wins over).
- Distance fog (Phase 7.2): `vb.render.set_fog{start=, ["end"]=}` (`end` is a
  Lua keyword, so it must be a quoted key) overrides the
  engine's default fog distance (rejects a call missing either field, or
  `end <= start`, and any call after `freeze()`). Absent an override, each
  client computes its own default from its own `view_distance` config
  (`end = view_distance * kChunkDim`, `start = end * 0.6`) — the server
  doesn't know each client's `view_distance`, so unlike
  `vb.physics.set_params`/`vb.daynight.set_curve` there's no server-side
  universal default this replaces, only a per-client fallback. Reaches
  joining clients as `S2C_FogParams`. Deliberately **no color field** — fog
  always blends into whatever `vb.daynight`'s current sky color already is
  (`sky_color_for_time()`), never an independently drifting tint.
- Read-only server config (Phase 6.13): `vb.config.get(key)` returns the
  operator's `server.toml`/CLI value for `key` — `bind_address`, `port`,
  `content_pack`, `max_players`, `view_distance`, `tick_rate`, `world_seed`,
  `gravity`, `void_kill_y`, `day_length_seconds`, `asset_max_file_mb`,
  `asset_max_total_mb`, `max_connections_per_ip` (§8.3 hardening, Phase 1.3
  polish — `0` = unlimited), `auth_mode` (`"none"`/`"token"`), or `motd`;
  `nil` for any other key. Deliberately **not** an override surface like
  `vb.physics.set_params`/`vb.daynight.set_curve` above — a pack can react to
  these values (e.g. tune spawn density to `view_distance`) but can't change
  what the operator running the server configured. `--singleplayer`'s
  in-process `PackRuntime` has no `ServerConfig`/`server.toml`, so every key
  returns `nil` there.

## Client UI API — `vb::script::UiRuntime` (`inc/vb/script/ui_runtime.hpp`, implemented, `VB_WITH_LUA`)

A second, separate `Vm` from the server's `PackRuntime` — pImpl'd the same
way, disabled-stub when `VB_WITH_LUA` is off. Drawing is split across the
core/render boundary: `UiRuntime` (`vb_core/script`) evaluates layout into a
plain `Widget` list (no raygui), and `vb::render::UiRenderer`
(`inc/vb/render/ui_renderer.hpp`) draws that list with raygui and reports
back which widgets fired an interaction — no sol2 in the render half.

- `ui.define(name, render_fn)` — `render_fn(state)` returns
  `{ widgets = { ... }, on_close = fn? }` and is called **once per UI frame**
  for as long as the screen is open (Phase 6.2; `raygui` is itself
  immediate-mode, so no diffing is needed). `state` is the *same* Lua table
  across every one of those frames — seeded once from `open()`'s `ctx_json`
  — so a widget callback (`on_click`/`on_change`) mutating `state` directly
  is naturally visible on the next frame with no extra plumbing. Use this
  for purely cosmetic, client-local state (selection highlight, scroll
  position); anything server-authoritative still goes over `ui.send_event`.
  A callback that wants a different screen should still `ui.close()` + have
  the server `open_ui()` again.
- Widget types implemented: `label`, `panel`, `button`, `textbox`, `list`
  (spec's named set minus **item grid**, deferred to 5.1 — needs real
  items), plus `rect` (Phase 6.16 — see below). Each widget table: `id`,
  `type`, `x`/`y`/`w`/`h`, `text` (label/panel/button/textbox), `items`/
  `list_index` (list only), and optional `on_click`/`on_change` callback
  fields.
- **`rect` — a raw filled/outlined rectangle, no baked-in meaning (Phase
  6.16).** `color = {r,g,b,a?}` (fill, default opaque white) and an optional
  `border = {r,g,b,a?}` (default: no border drawn). Unlike every other
  widget type, this one carries no semantic ("this is a progress bar", "this
  is a health bar") at all — it's the one primitive deliberately left
  meaning-free so a pack can compose *any* purely-visual element (a progress
  bar, a divider, a health bar segment, a colored swatch) out of one or more
  of them, entirely in Lua, rather than the engine shipping a
  `progress_bar`/`health_bar`/... widget type per use case.
- **HUD (Phase 6.16): `ui.define_hud(render_fn)`** — registers a single
  always-on overlay, separate from `ui.define`'s named-screen registry above.
  Unlike a modal screen, it's never `open()`/`close()`'d: `render_fn(state)`
  is evaluated every UI frame unconditionally (its own persistent `state`
  table, untouched by any modal screen opening/closing alongside it) and
  drawn regardless of whether a modal screen is also up. `content/base/ui/
  hud.lua` uses this + two `rect` widgets (a background/border, and a fill
  sized by the raw fraction below) to render the hold-to-break progress bar
  — "engine provides raw state, Lua deals with presentation": the engine
  still owns the actual hold-timer/reach/target-tracking logic (that's
  gameplay input handling), and offers only the meaning-free `rect`
  primitive, not a "progress bar" concept of its own.
- **Raw client-local state: the `client` table** (Phase 6.16, sibling to
  `ui` above) — read-only engine state a HUD (or any UI script) can query;
  nothing here draws a pixel, callers decide whether/how to show it:
  - `client.break_progress()` — `nil`, or `0..1` while the player is holding
    to break a targeted block.
  - `client.screen_size()` — `{width=.., height=..}`; widgets take absolute
    pixel positions like everywhere else in this API, so centering something
    in a HUD needs the real window size rather than a hardcoded guess.
- `ui.send_event(kind, value)` — sends a `C2S_UiEvent` to the server
  (`current_name`/the widget whose callback is currently running are filled
  in automatically). `ui.close()` — always sends one `"close"` event, then
  runs the layout's own `on_close` (if any) for local cosmetic cleanup, then
  clears state.
- Client wiring (`src/client/main.cpp`): every synced `ui/*.lua` file
  (`ClientSession::virtual_pack_fs()`, Asset Sync/Phase 4.4) is loaded into
  `UiRuntime` right after join; `ClientSession::take_open_ui()` then drains a
  pending `S2C_OpenUi`, `UiRuntime::open()` evaluates the named layout, and
  `UiRenderer::draw()` runs once per frame while open, between the 3D pass
  and `window.end_frame()`; interactions route back through
  `report_click`/`report_change`/`report_list_change`. `content/base/ui/
  pause.lua` (`base:pause`) and `ui/inventory.lua` (`base:inventory`) are
  real, loadable screens — **known gap:** nothing in real gameplay currently
  *opens* either one (no client gesture or `C2S` message requesting "open my
  inventory"/"pause" exists; `player:open_ui` is server-push-only), and
  `ui/inventory.lua` renders raw numeric item ids (a `list` widget, not the
  spec's item-grid, §10.4's own still-missing widget type) rather than
  names, for the same `BlockId`-as-item-space reason noted above. The
  mechanism itself is exercised by `tests/unit/ui_runtime_test.cpp` and an
  end-to-end `player:open_ui` → click → `vb.on("ui_event", ...)` round trip
  in `tests/unit/pack_runtime_integration_test.cpp`. `ui/hud.lua` (`Phase
  6.16`) is the always-on HUD, loaded the same way; `--singleplayer` reads
  every `ui/*.lua` file straight off disk (`kSingleplayerContentPack`)
  rather than through Asset Sync, since it never asset-syncs at all — a
  real multiplayer connection still uses `virtual_pack_fs()`.

## Worked example — `content/base`

`content/base` is the pack `voxel_browser_server` loads by default and the
target of `tests/unit/content_pack_test.cpp`'s regression coverage (it loads
the real files, not inline Lua strings). Read it alongside this doc rather
than as a black box — every file is commented explaining *why*, not just
*what*. Load order (`src/script/pack_loader.cpp`): `blocks/*.lua` →
`entities/*.lua` → `biomes/*.lua` → any other root-level `*.lua` file
(sorted) → `init.lua` last.

| File(s)                              | Demonstrates                                            |
| ------------------------------------- | -------------------------------------------------------- |
| `blocks/dirt.lua`, `grass.lua`, etc.  | `vb.register_block`, idempotent re-declaration of the Phase 2 base set, `on_break` calling `vb.world.spawn_item_drop` |
| `blocks/planks.lua`, `sticks.lua`     | Registering genuinely new, crafted-only blocks (not a re-declaration) |
| `crafting.lua`                        | A full, working game system (recipes, ingredient checks, a `/craft` chat command) built entirely in content on top of `vb.register_craft` + `player:give`/`take` — **the reference example of "game rules belong in a pack, not the engine"** |
| `entities/dropped_item.lua`           | `vb.register_entity` — real dispatch since Phase 6.1 (`vb.world.spawn`/`on_spawn`/`on_tick`/`on_hit`/`on_death` all fire, no EnTT registry involved), but nothing in `content/base` itself ever calls `vb.world.spawn("base:dropped_item", ...)` — real block drops still go through the separate, already-working `vb.world.spawn_item_drop` hardcoded path above instead. `content/examples/kitchen_sink/entities/sentry.lua` (Phase 6.15) is the worked example of a pack actually spawning/hitting/killing one of these |
| `biomes/plains.lua`, `forest.lua`     | `vb.register_biome`: has a real consumer as of Phase 6.14 (`vb.worldgen.set_pipeline`), but `content/base` itself still never calls `set_pipeline` — these stay declarative-only *in this pack*, kept minimal/production-shaped per spec §5.1; a worked pipeline example belongs to 6.15's separate demo pack |
| `ui/inventory.lua`, `ui/pause.lua`    | `ui.define`, real screens loaded by every connecting client |
| `ui/hud.lua`                          | `ui.define_hud` + the `client.*` raw-state table (Phase 6.16) — the always-on hold-to-break progress bar |
| `init.lua`                            | Pack-wide setup that isn't a single registration — `vb.storage` persisting a boot counter across restarts |

If you're writing a new pack: copy `content/base`'s directory layout, keep
`pack.toml`'s `entry = "init.lua"` (not yet read — `load_content_pack` is the
real entry point until `require` exists, §10.2), and remember the loader
only auto-loads `blocks/`, `entities/`, `biomes/`, and root-level `*.lua`
files — anything under `ui/`/`textures/` is loaded client-side over Asset
Sync instead, never by the server's `load_content_pack`.

## Worked example — `content/examples/kitchen_sink` (Phase 6.15)

A second, sibling worked example — **not** loaded by default by anything,
point an operator's `--content-pack`/`server.toml` at
`content/examples/kitchen_sink` to run it. Where `content/base` stays
minimal/production-shaped (spec §5.1) and each Phase 6 feature it touches is
demonstrated only incidentally, this pack's entire purpose is the opposite:
exercise every Phase 6 "default + override" API at least once, each with a
short comment naming the exact phase/section it demonstrates (see its
`init.lua` for the full file-by-file index) — a working reference that
`tests/unit/kitchen_sink_pack_test.cpp` regression-tests the same way
`content_pack_test.cpp` protects `content/base`. Covers: `register_block`'s
`max_damage`/`max_stack`/`pickup_radius`/`item_lifetime_seconds` together on
one block (`blocks/unstable_ore.lua`), `register_entity` +
`vb.world.spawn` — real and dispatched since Phase 6.1 (2026-09-17), a
`/sentry` chat command actually spawns one and `on_tick`/`on_hit`/`on_death`
all really fire (`entities/sentry.lua`; `content/base/entities/
dropped_item.lua`'s own "nothing calls them" comment predates 6.1 and is now
stale — not this pack's file to fix), `register_biome` with real
`probability`/`adjacency`
(`biomes/savanna.lua`/`tundra.lua`) feeding a real `vb.worldgen.set_pipeline`
+ `vb.noise.*` graph with a carver and a vein (`worldgen.lua`) — the worked
pipeline example `content/base`'s own biome files point to —
`vb.physics.set_params` (`physics.lua`), `vb.daynight.set_curve`/
`set_day_length` (`daynight.lua`), `vb.db`/`vb.crypto.hash` plus a
`block_break_tick` handler (`mechanics.lua`), `player_death` with custom
`heal`/`message`/`drop_inventory` (`death.lua`), chat text-rewriting
(`chat.lua` — contrast with `content/base/crafting.lua`'s veto-only use of
the same event), and `register_keybind` + `player_input` opening a
`ui.define` screen via `player:open_ui` (`keybinds.lua` + `ui/status.lua`).

## Audio / sfx — not implemented (v0 has no audio subsystem)

There is no sound/music API, client-side or server-side, and no engine code
plays audio anywhere. This is a deliberate v0 scope cut, not an oversight:

- `cmake/Dependencies.cmake` builds raylib with
  `SUPPORT_MODULE_RAUDIO OFF` — raylib's audio module (`InitAudioDevice`,
  `PlaySound`, `LoadMusicStream`, ...) is compiled out entirely, so it isn't
  even linkable from `vb_render` today, let alone exposed to Lua.
- `ARCHITECTURE_SPEC.md` §10.5's block-break event-flow diagram mentions
  "sfx trigger" as an example of what a pack's `on_break` callback might
  *eventually* do (illustrative, alongside "drops") — it was never a real
  hook and nothing dispatches it. `register_block`'s `on_break`/`on_place`
  callbacks (Phase 4.2, `PackRuntime`) exist and fire for real, but a pack
  has no `vb.`/`ui.` call it could make from inside one to actually produce
  a sound.
- There is no `S2C_PlaySound`-shaped wire message, and no client-side sound
  cache/loader analogous to `ClientAssetCache` for textures/scripts (Phase
  4.4) — packaging sound assets for asset sync would need one.
- Tracked as a first-class deferred item, not folded into any phase's
  backlog: `REMAINING_TASKS.md`'s "Deferred (post first-playable)" list has
  "Audio subsystem + Lua sfx/music API" as its own line.

When this lands, the natural shape (unconfirmed, not designed) would mirror
`player:send_message`/`open_ui`: a server-authoritative
`entity:play_sound(name, opts?)` / `vb.world.play_sound_at(pos, name)` call
that sends a small S2C message, with sound files traveling over the existing
Asset Sync pipeline (Phase 4.4) like any other pack asset — but none of that
exists yet.

## Sandbox

Implemented in `Vm` construction (`strip_sandbox`): opens only `base`, `string`,
`table`, `math`, `coroutine`, `utf8`, then nils `os`, `io`, `dofile`, `loadfile`,
`load`, `loadstring`, `collectgarbage`, `require`, `package`, and every `debug.*`
except `traceback`. Instruction-count hook (`lua_sethook` / `LUA_MASKCOUNT`) and a
ceiling allocator are always on.

Still to do (Phase 4.4): reimplemented `require` over the synced virtual pack FS;
per-callback wall-clock budget enforced by the tick loop; the separate restricted
client UI VM. See §10.2.
