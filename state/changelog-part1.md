# Changelog (detail) — Part 1: Phase 6 features, investigations, Phase 0-4 bootstrap

> Full landed-feature/bugfix writeups. See `STATE.md` for current status and pointers.
> Chronological, newest first (as originally written). Covers: Phase 6.1/6.4/6.5/6.6/6.7/6.8/6.9/6.11/6.16
> landed features; the 2026-09-15 driver-corruption/FPS/lighting/meshing-threading/GNS-send-buffer
> investigations; the 2026-09-10/11 Phase 0-4 bootstrap entries; "Gotchas learned this pass".
> Continued in `state/changelog-part2.md` (Phase 5.1 onward + Phase 6 design passes + librg).

---

- **2026-09-18 — Phase 6.11 item drop parameters landed (uncommitted).**
  See this file's header entry above for the full writeup; summary: no wire
  message changed, no `kEngineProtocolVersion` bump — this is a pure
  server-side default+override, same shape as 6.7/6.8/6.9's "default +
  override" items. `REMAINING_TASKS.md` 6.11 marked done.

- **2026-09-18 — Phase 6.9 inventory stacking landed (uncommitted).**
  `REMAINING_TASKS.md` 6.9: no max stack size existed anywhere —
  `PlayerHandle::give()` always pushed a brand-new slot, never combined into
  an existing one.
  **Where the override lives, and why not `register_item`:** the task doc's
  own suggestion was "let `register_item` override its own stack size," but
  `vb.register_item` never allocates an id space at all (a known, already-
  documented gap — see `content/base/blocks/planks.lua`'s comment) and
  nothing holdable is ever actually a "registered item" in practice, only a
  registered block. Added `BlockType::max_stack`
  (`inc/vb/world/block.hpp`, default = a new `world::kDefaultMaxStackSize`
  constant, 64) instead, overridable via `vb.register_block{max_stack = N}`
  — identical def-parsing shape to 6.5's `max_damage` field
  (`src/script/pack_runtime.cpp`'s `register_block` lambda).
  **New shared helper, not just a `give()` rewrite:** `PackRuntime::Impl::
  give_item(NetId, BlockId, count)` is now the one place that actually
  credits an inventory — fills existing slots holding that item that are
  under `max_stack` first (oldest slot first, first-fit, no attempt at
  optimal packing), then keeps starting new slots sized up to `max_stack`
  for whatever's left. `PlayerHandle::give()` (Lua `player:give()`) and the
  item-pickup handler installed in `attach_session()` (a player walking over
  a dropped item) both now call it — previously two separate hardcoded
  `slots.push_back(...)` call sites that happened to do the same thing by
  coincidence, even though the pickup handler's own existing comment
  claimed pickup "credits their inventory exactly like player:give() does."
  Neither `give_item()` nor its two callers call `sync_inventory()`
  internally — each caller still pushes its own `S2C_Inventory` snapshot
  once afterward, unchanged from before.
  **`take()` untouched:** already worked across multiple slots correctly
  (6.6-era code, see its own test); stacking only changes how slots are
  *created*, not how they're consumed.
  **Same idempotent-registration caveat 6.5 already documented for
  `max_damage` applies here too, not re-litigated:** re-registering an
  existing block name (e.g. one of the base 8) with a different `max_stack`
  silently does nothing, since `BlockRegistry::add_or_get` is idempotent by
  name and never updates an existing entry's properties.
  **Scoped down, deliberately:** no cap on total inventory *slot count* —
  the task item's "stack cap ... and slot count" phrasing reads as one
  thing (how many items fit in one slot) rather than two, and there's no
  other evidence in the spec of an intended separate max-slots limit; not
  added.
  **Verification:** 2 new `tests/unit/pack_runtime_test.cpp` cases — two
  40-count `give()`s of a default-stack item combine into a 64 slot + a
  16-count overflow slot (not four separate ones); a `max_stack = 1` block
  registered via `register_block` keeps two single-item `give()`s as two
  separate slots. Full `vb_tests` green on `build-net-lua`
  (`VB_WITH_NET=ON`, `VB_WITH_LUA=ON`, 245/245 cases). All 4 CTest cases
  (`vb_tests`/`server_smoke`/`client_smoke`/`singleplayer_smoke`) pass; both
  binaries rebuild clean. Not verified under a no-Lua/ASan config this
  session (no pre-built ASan dir remains on this machine, per 6.4's entry
  above) — the Lua-specific binding (`register_block`'s `max_stack` field
  parse) is entirely inside `pack_runtime.cpp`'s existing `#if VB_WITH_LUA`
  region; `BlockType::max_stack`'s default member initializer and
  `give_item()`'s logic have no Lua dependency, so the stub-build risk is
  low, just not re-confirmed here.

- **2026-09-18 — Phase 6.16 client-local HUD mechanism landed
  (uncommitted), user-requested.** The user noticed the hold-to-break
  progress bar (5.2) was still hardcoded C++ (`DrawRectangle` calls in
  `src/client/main.cpp`) despite Phase 6's Lua-extensibility push, and asked
  for it to move to Lua per the project's "engine provides raw state, Lua
  deals with presentation" rule.
  **Why a new mechanism was needed, not just a Lua rewrite:**
  `vb::script::UiRuntime`'s existing model (`ui.define(name, fn)` +
  `open()`/`close()`) is for server-pushed *modal* screens (inventory,
  pause) — there's no concept of an always-on overlay independent of that.
  Added one: `ui.define_hud(render_fn)` registers a single render function
  evaluated every UI frame unconditionally (not gated on `is_open()`), with
  its own persistent `state` table that a modal screen opening/closing
  alongside it doesn't touch. `UiRuntime::render_hud()` evaluates it;
  `evaluate_frame()`'s widget-table-to-`Widget` parsing was extracted into a
  shared `widget_from_table()` free function so `evaluate_hud_frame()`
  doesn't duplicate it.
  **New widget type, revised once already (see below): `WidgetType::kRect`.**
  The first pass shipped a `kProgressBar` type (`Widget::value`, drawn via
  raygui's `GuiProgressBar`) — the user immediately called this out as still
  baking a presentation *concept* into the engine (the engine "knows" what a
  progress bar looks like; Lua only supplied a number). Replaced with
  `kRect`: a raw filled rectangle (`fill_r/g/b/a`) with an optional 1px
  outline (`border_r/g/b/a`, alpha 0 = none), drawn with plain
  `DrawRectangle`/`DrawRectangleLines` — no raygui control, no semantic
  meaning at all. `content/base/ui/hud.lua` now builds the bar from *two*
  `rect` widgets (a background+border rect, and a fill rect whose width is
  `bar_w * progress`) itself; the engine has no idea a "progress bar" exists,
  it just draws boxes where it's told to. `widget_from_table()` parses
  `color = {r,g,b,a?}`/`border = {r,g,b,a?}` the same 1-indexed-array
  convention `vb.daynight.set_curve`'s `color` field already established.
  **Raw state relay, the actual "engine provides raw state" part** (this
  half was correct in the first pass and unchanged by the revision): a new
  `client` top-level table in the UI Lua VM (`src/script/ui_runtime.cpp`,
  sibling to the existing `ui` table) exposes `client.break_progress()`
  (nil, or 0..1) and `client.screen_size()` (`{width=.., height=..}`).
  `src/client/main.cpp`'s hold-timer/reach/target-tracking *logic* is
  completely unchanged from 5.2 (still engine-side, since input handling and
  reach validation are gameplay, not cosmetics) — it now just calls
  `ui_runtime.set_break_progress(...)`/`set_screen_size(...)` once per frame
  instead of computing pixel rectangles itself.
  **Second `UiRenderer` instance, not incidental:** drawing both the modal
  screen and the HUD through one `UiRenderer` would thrash its
  per-widget-id text-box/list-selection edit-state caches every single
  frame — `UiRenderer::draw()` clears them whenever the drawn `ui_name`
  differs from the previous call, and a modal name alternating with `"hud"`
  every frame is exactly that. `src/client/main.cpp` now holds
  `ui_renderer` (modal) and `hud_renderer` (HUD) as two separate instances.
  **Real bug found while wiring this in, bigger than the requested
  change:** `--singleplayer` never asset-syncs (no `PackRuntime`/manifest on
  that in-process path, a pre-existing 4.3 gap) — the `enter_playing` lambda
  only ever populated `ui_runtime` from `client->virtual_pack_fs()`, which
  is always empty for singleplayer. This meant `base:pause`/`base:inventory`
  (and now the new hud.lua) silently never loaded in singleplayer at all,
  the single most common dev/test path — not a regression from this
  session's work, but this session's work would have been invisible without
  fixing it, so it was fixed: singleplayer now reads `ui/*.lua` directly off
  `kSingleplayerContentPack` from disk (client and the integrated server
  share one filesystem there, so there's nothing to actually "sync"); the
  real-multiplayer branch is untouched.
  **Verified live twice, not just by unit test** (this repo's Lua UI
  features have no automated GL-context coverage — same limitation 5.2's
  original entry noted): once against the original `kProgressBar` version,
  again after the `kRect` revision — both times launched the real
  `voxel_browser.exe --singleplayer` windowed binary, captured the mouse,
  held LMB on a targeted block, and screenshotted mid-hold. The revised
  version shows the same visual (a light fill growing over a dark
  background+border), now composed from two independent `rect` widgets
  instead of one raygui control; console log both times showed zero "ui
  pack file failed to load" lines (`hud.lua`/`pause.lua`/`inventory.lua` all
  loaded cleanly). Screenshots not saved to the repo.
  **Unit tests**: `tests/unit/ui_runtime_test.cpp` gained 4 cases —
  hud renders nothing until `set_break_progress` is set and hides again on
  `nullopt`, `render_hud()` is a safe no-op when `ui.define_hud` was never
  called, the hud's `state` table persists across calls independent of a
  modal screen's open/close cycle, and the disabled-build stub's
  `set_break_progress`/`set_screen_size`/`render_hud` all no-op cleanly.
  **Verification:** full `vb_tests` green on `build-net-lua`
  (`VB_WITH_NET=ON`, `VB_WITH_LUA=ON`), all 4 CTest cases
  (`vb_tests`/`server_smoke`/`client_smoke`/`singleplayer_smoke`) pass, plus
  the live windowed run above.
  **Deliberately not attempted:** hud widgets aren't wired to
  `report_click`/`report_change` (no interactive HUD element exists yet to
  need it); the player list/chat box/hotbar (`src/client/main.cpp`'s other
  always-on HUD pieces, predating this session) are still hardcoded C++ —
  only the break-progress bar was in scope. Migrating the rest to
  `ui.define_hud` (replacing `draw_overlay` entirely with a real Lua HUD) is
  a natural, larger follow-up tracked in `REMAINING_TASKS.md` 6.16's last
  bullet, not started here.

- **2026-09-18 — Phase 6.8 day/night cycle curve landed (uncommitted).**
  `sky_brightness()`/`sky_color_for_time()` (`inc/vb/world/daynight.hpp` +
  `src/world/daynight.cpp`) were a fixed 4-keyframe gradient with no Lua
  reach at all; generalized into `vb::world::DayNightCurve` (a
  `vector<DayNightKeyframe>` of `{tick, brightness, color}`) with curve-taking
  overloads of both functions. `default_day_night_curve()` reproduces the
  exact 4 keyframes the old hardcoded tables had, and an *empty* curve passed
  to either overload falls back to it — so every pre-6.8 call site (both the
  existing no-curve overloads, kept as thin wrappers, and `daynight_test.cpp`'s
  existing assertions) is byte-for-byte unaffected.
  **Lua surface**: `vb.daynight.set_curve{keyframes = {{tick=, brightness=,
  color={r,g,b}}, ...}}` (`src/script/pack_runtime.cpp`) stores the raw table;
  `PackRuntime::effective_day_night_curve()` parses it into a
  `world::DayNightCurve` lazily (mirrors `effective_move_params()`'s posture
  of reading `move_params_table` field-by-field on demand rather than
  eagerly), returning `nullopt` if no pack ever called it. Rejects an
  empty/missing `keyframes` table and any call after `freeze()`.
  **Replication, new wire message**: `S2C_DayNightCurve` (id 51,
  `inc/vb/protocol/world.hpp`, `kEngineProtocolVersion` 14 → 15) mirrors
  `DayNightKeyframe` flat (`varint n` + `n × {u32 tick, f64 brightness, u8 r,
  u8 g, u8 b}`) rather than protocol/ depending on world/ — same posture as
  `S2C_MoveParams` mirroring `physics::MoveParams`. Sent between `C2S_Ready`
  and `S2C_JoinAccept` via a new `HandshakeServerHost::day_night_curve` hook
  (`nullopt` default = no frame at all, so a host/test that never opts in
  leaves the client on `default_day_night_curve()`, unchanged) — wired in
  both `src/server/main.cpp` and `--singleplayer`'s `make_singleplayer_host`
  (`src/client/main.cpp`), each reading `pack_runtime.effective_day_night_curve()`
  once and capturing the result by value in the lambda (same pattern
  `host.move_params` already used). `ClientSession::apply_day_night_curve`
  (`src/net/session.cpp`) rebuilds the `world::DayNightCurve` and stores it in
  a new `day_night_curve_` member, exposed via a `day_night_curve()` const
  getter; intercepted unconditionally in `tick()` (not gated on handshake
  state), same reasoning as `S2C_MoveParams`/`S2C_BlockRegistry` — real
  transports don't guarantee cross-lane arrival order relative to
  `JoinAccept`. `src/client/main.cpp`'s sky-clear code now reads
  `vb::world::sky_color_for_time(client->time_of_day(), client->day_night_curve())`
  instead of the bare single-argument overload, so a pack's override actually
  reaches the rendered sky, not just server-side bookkeeping.
  **Second half of the gap, also closed in this session (not originally
  strictly required, but named in the same task item and cheap once the
  curve plumbing existed):** `day_length_seconds` had a real runtime setter
  (`ServerSession::set_day_length_seconds`) that literally nothing ever
  called — every server (dedicated and `--singleplayer` alike) silently ran
  on the hardcoded `1200.0` member-initializer default with no way to change
  it. Extracted that literal to a new `vb::net::kDefaultDayLengthSeconds`
  constant (`inc/vb/net/session.hpp`) so `ServerSession`'s own default and
  `--singleplayer`'s host (which has no `server.toml` to read a config value
  from) can't drift apart. Added `ServerConfig::day_length_seconds` (`server.toml`,
  same default) as the config-layer base; `vb.daynight.set_day_length(seconds)`
  (rejects `seconds <= 0`) overrides it via a new
  `PackRuntime::effective_day_length_seconds(base)` — the exact same
  config-then-pack-override shape 6.7 established for `gravity`/
  `vb.physics.set_params`, not a new pattern. `src/server/main.cpp` calls
  `session.set_day_length_seconds(pack_runtime.effective_day_length_seconds(
  config.day_length_seconds))`; `Singleplayer`'s constructor body calls the
  equivalent with `vb::net::kDefaultDayLengthSeconds` as the base.
  **Deliberately not attempted:** a pack-supplied arbitrary curve *function*
  (a Lua callback re-evaluated per read) — every other Phase 6 "default +
  override" item ships data (a table of values), not an executable hook
  re-invoked from the replication path, and piecewise-linear keyframes can
  already approximate most shapes with enough points.
  **Verification:** full `vb_tests` green on `build-net-lua`
  (`VB_WITH_NET=ON`, `VB_WITH_LUA=ON`, 240/240 cases, up from 230 at 6.7's
  count — this session's 4 new `daynight_test.cpp` cases, 4 new
  `pack_runtime_test.cpp` cases, 1 new `protocol_test.cpp` case, and 1 new
  `config_test.cpp` case). All 4 CTest cases
  (`vb_tests`/`server_smoke`/`client_smoke`/`singleplayer_smoke`) pass. Not
  verified under a no-Lua/ASan config this session (no pre-built ASan dir
  remains on this machine, per 6.4's entry) — the Lua-specific bindings are
  entirely inside `pack_runtime.cpp`'s existing `#if VB_WITH_LUA` region
  (with a matching stub returning `base`/`nullopt` in the `#else` branch,
  compiled but not exercised here), and every other changed file
  (`daynight.cpp`, `world.cpp`, `handshake.cpp`, `session.cpp`) has no Lua
  dependency at all, so the no-Lua stub-build risk is low, just not
  re-confirmed here.

- **2026-09-18 — Phase 6.7 physics/movement parameters landed
  (uncommitted).** `vb.physics.set_params{...}` (`src/script/pack_runtime.cpp`)
  lets a pack override any `physics::MoveParams` field, per
  `REMAINING_TASKS.md` 6.7 and the struct's own long-standing "a Lua pack
  overrides per entity kind" comment.
  **Scoped to one global override, not per-entity-kind, on purpose:** no
  entity kind besides the player runs `step_movement` today (6.1's script
  entities have no physics at all) — a per-kind table would have nowhere
  else to apply. `PackRuntime::effective_move_params(base)` reads the raw
  `sol::table` field-by-field with `get_or(name, base.field)`, so a field the
  pack never set falls back to whatever `base` the caller passed in, not a
  fixed literal.
  **`ServerConfig.gravity` reconciliation, decided:** it's the *base*
  `effective_move_params()` is called with (`move_params.gravity =
  config.gravity` before the override runs, `src/server/main.cpp`) — the
  operator's `server.toml` sets the engine default, an explicit pack
  override wins over it if the pack sets `gravity` itself.
  **The actual bug this session found, bigger than the task as scoped:**
  auditing "does the client's prediction ever see this" turned up that it
  never did, for *any* value of `MoveParams` — not just a hypothetical
  future pack override, but `ServerConfig.gravity` itself. Every client
  (dedicated-server and `--singleplayer` alike) constructs its own
  `physics::MoveParams{}` locally and only ever calls
  `ClientSession::set_move_params()` with that hardcoded default; nothing
  on the wire carried the server's actual tunables. Client-side prediction
  running with the wrong gravity mostly hides behind
  `ClientSession::reconcile()` snapshotting the position back to server
  truth every tick, so this was invisible without measuring — worth
  remembering as a category: a value that only affects *prediction* (not
  correctness, since the server is authoritative regardless) can silently
  diverge for a long time before anyone notices the jitter.
  **Fix — new wire message**: `S2C_MoveParams` (id 50,
  `inc/vb/protocol/world.hpp`, `kEngineProtocolVersion` 13 → 14) mirrors
  `physics::MoveParams` flat (13 `f64` fields + `bool fly`) rather than
  protocol/ taking a dependency on physics/ — same posture as
  `BlockRegistryRecord` mirroring `world::BlockType`. Sent between
  `C2S_Ready` and `S2C_JoinAccept` alongside `S2C_BlockRegistry`/
  `S2C_KeybindRegistry` via a new `HandshakeServerHost::move_params` hook
  (`nullopt` default = no frame, so every existing host/test is unaffected).
  `ClientSession::apply_move_params` (handled unconditionally in `tick()`,
  same as block/keybind registry, since real transports don't guarantee
  cross-lane ordering) converts it straight into `move_params_`.
  **Second bug found while wiring the fix in, not just reasoned about:**
  two call sites in `src/client/main.cpp` (`run_headless`'s single-connection
  path and the windowed `enter_playing` lambda) constructed a *fresh*
  default `physics::MoveParams` and called `client->set_move_params()` with
  it right after checking `join_accept()` — i.e., *after* `S2C_MoveParams`
  had already arrived and been applied (it travels in the same handshake
  step as `JoinAccept`), immediately overwriting the real value back to the
  hardcoded default. Neither call site needed to construct a MoveParams at
  all; both were just trying to get *some* value into a local variable used
  for eye-height math. Fixed by adding a `ClientSession::move_params()`
  const getter and reading that back instead
  (`move_params = client->move_params();`) — the general lesson: once a
  session applies something unconditionally as soon as it arrives, any
  later "set it up for use" code must read it back, not reconstruct a
  default and reassert it.
  **`--singleplayer` wiring**: `Singleplayer`'s in-process host needed the
  same `host.move_params` hook as the dedicated server
  (`make_singleplayer_host`, `src/client/main.cpp`) — added a `move_params`
  member (declared right after `pack_runtime`, so member-init order lets it
  be computed via `pack_runtime.effective_move_params(vb::physics::
  MoveParams{})` before `server` is constructed) and an explicit
  `server.set_move_params(move_params)` call in the body (the dedicated
  server already did this; the integrated server previously didn't call
  `set_move_params` at all, silently running server-side physics on
  `MoveParams{}`'s hardcoded defaults too — not just a client-side gap).
  **Verification:** full `vb_tests` green on `build-net-lua`
  (`VB_WITH_NET=ON`, `VB_WITH_LUA=ON`, 230/230 cases, up from 225 — this
  session's 2 new `protocol_test.cpp` cases, 2 new `block_registry_test.cpp`
  cases, and 2 new `pack_runtime_test.cpp` cases). All 4 CTest cases
  (`vb_tests`/`server_smoke`/`client_smoke`/`singleplayer_smoke`) pass. Ran
  the real `voxel_browser.exe --headless --singleplayer` binary directly
  (not just ctest) and confirmed the log line `received move params
  (gravity=28)` appears right after join, proving the new wire path fires
  in the actual integrated-server path, not just in a unit test's
  `LoopbackNetwork`.
  **Not done, deliberately deferred:** per-entity-kind override (no second
  physics-driven kind exists to need it, see above); 6.8-6.13's other
  "default + override" items (day/night curve, inventory stacking, chat
  transform, item-drop params, anim clip priority, read-only config
  visibility) are separate, untouched follow-ups in the same phase-6 shape.

- **2026-09-18 — Phase 6.5 shared block-damage breaking landed
  (uncommitted).** `vb.register_block{max_damage=N}` (default 0 = today's
  instant break) opts a block into a shared damage pool per
  `ARCHITECTURE_SPEC.md` §10.7 / `REMAINING_TASKS.md` 6.5.
  **Scoped down from the full spec on purpose:** the spec's design has the
  damage *value* itself ride the interest/replication system as a transient
  entity so nearby players see cracks form. That value has exactly one
  consumer (a crack overlay), which is itself blocked on the still-missing
  texture/atlas system (4.3/5.1 — client renders untextured cubes), so this
  session implemented only the begin/tick/complete *mechanism* —
  `vb::world::BlockDamageSystem` (`inc/vb/world/block_damage.hpp` +
  `src/world/block_damage.cpp`, pure/dependency-free/unit-tested, same
  posture as `ItemDropSystem`) tracks `pos -> {damage, max_damage,
  last_touched_tick, contributors}`, but its `changed`/`cleared`
  `BlockDamageTickResult` fields are computed and then ignored by
  `ServerSession::update_block_damage` — there's no wire message to put them
  on yet. Wiring that up is the natural next step once a client exists to
  show it to.
  **New wire messages**: `C2S_BlockBreakBegin{pos, face}` /
  `C2S_BlockBreakStop{pos}` (ids 48/49, `inc/vb/protocol/world.hpp`) bracket
  a player holding a target; `BlockRegistryRecord` also gains `max_damage`.
  `kEngineProtocolVersion` 12 → 13 (`cmake/version.hpp.in`,
  `docs/protocol.md`).
  **Server flow**: `ServerSession::handle_block_break_begin` gates entry —
  the same reach check `apply_block_edit` uses (extracted to a new public
  `WorldReplicator::in_reach`, since `C2S_BlockBreakBegin` doesn't itself
  mutate the world the way `apply_block_edit` does) plus an engine-level
  `max_damage > 0` check, then a `vb.on("block_break_begin", ...)` veto via
  a new `ServerSession::BlockBreakHooks` struct (same `std::function`-seam
  pattern as `BlockEditHooks`/`set_chat_handler`/etc. — no sol2 in
  `session.hpp`). `ServerSession::update_block_damage()` (called from
  `tick()` alongside `update_item_drops`) drives `vb.on("block_break_tick",
  ...)`/`vb.on("block_health_tick", ...)` once per server tick per
  damaged block; on completion (summed damage reaches `max_damage`) it
  synthesizes a `C2S_BlockEdit{kBreak}` and calls the *existing*
  `WorldReplicator::apply_block_edit` — the same `on_break`/`S2C_ChunkDelta`
  path a manually-sent edit uses, attributed to whichever player happened to
  be contributing when it completed (`BlockDamageSystem::CompletedBreak`
  captures one, arbitrary among concurrent "breaking together" contributors,
  at the moment `states_` erases the entry — captured *before* erasure since
  the completing tick's contributor list wouldn't otherwise survive past
  `tick()`'s return).
  **`vb.on` semantics, not veto-shaped**: `block_break_tick`/
  `block_health_tick` return numbers, not booleans — multiple concurrent
  *players* contributing sum their deltas per the spec, and this
  implementation also sums multiple *handlers* registered for the same
  event the same way (an orthogonal case the spec didn't call out;
  summing rather than picking one avoids silently dropping a registered
  handler's contribution). `block_health_tick`'s chain instead takes the
  *last* handler's non-nil return, mirroring `run_player_input`'s
  replacement-chaining style.
  **Idempotent-registration trap hit while writing the integration test,
  not just reasoned about:** `BlockRegistry::add_or_get` (existing code,
  unchanged) returns an already-registered name's id unchanged — it never
  applies the new `BlockType` passed in. Re-registering `"base:grass"` with
  `max_damage = 3` in a test does *nothing*, since `base:grass` already
  exists from `BlockRegistry::base()`. Fixed by registering a brand-new
  block name (`"test:crumbly"`) instead and placing it into the world
  directly (`world.set_block`, bypassing worldgen, which only ever emits
  the 8 base ids) — worth remembering for any future test (or pack) that
  wants to change a property of one of the base 8 blocks specifically:
  it silently won't take effect the way `vb.register_item`/`vb.register_biome`
  callers might expect from the "idempotent by name" framing.
  **Construction-order trap, also hit while writing the test:** `World`
  takes `BlockRegistry` *by value* (copies it at construction) — a new block
  a pack registers via Lua only exists in a `World`'s internal registry copy
  if the `World` is constructed *after* `PackRuntime::freeze()`, matching
  `src/server/main.cpp`'s real order. The existing integration tests in this
  file construct `World` *before* `PackRuntime`/`load_pack_file`/`freeze()`
  and get away with it only because they never register a genuinely new
  block id (just re-register existing base names for `on_break`/veto
  purposes, which doesn't need `World`'s registry copy to know about it) —
  copy this file's *newest* two tests' construction order, not the older
  ones, for anything that needs a Lua-registered block to actually exist in
  the live world.
  **Test-content-loading trap** (unrelated to the above, cost a separate
  debugging round): checking `world.solid_at(pos)` for a not-yet-loaded
  chunk silently returns `false` (unloaded reads as air) — a
  `REQUIRE(world.solid_at(far_away_pos))` placed *before* moving a player
  near it and pumping enough ticks to load that chunk fails with no hint
  why. The existing "vetoes a block break" test in this same file already
  gets this right (moves the player first, pumps, *then* asserts) — a new
  test copied the assert-then-move order by mistake and failed until
  reordered to match.
  **Not done, deliberately deferred** (all noted inline in
  `REMAINING_TASKS.md` 6.5 too): `crack_texture` field on `BlockType` (no
  texture/model fields exist on it at all yet, nowhere to put it); the
  damage-value replication channel (see the scoping note above); any
  content in `content/base` using `max_damage` (every base block keeps
  `max_damage == 0`, so the existing 5.2 client-side 0.35s hold-to-break
  timer in `src/client/main.cpp` is completely untouched and still governs
  every real block in the shipped game today); no client UI sends
  `C2S_BlockBreakBegin`/`Stop` yet, though `ClientSession::
  send_block_break_begin`/`send_block_break_stop` are real, tested wire
  calls ready for one.
  **Verification:** full `vb_tests` green on `build-net-lua`
  (`VB_WITH_NET=ON`, `VB_WITH_LUA=ON`, 225/225 cases, up from 215 at
  6.4's count — the delta is this session's `block_damage_test.cpp` (7
  cases) + 2 new `protocol_test.cpp` cases + 2 new
  `pack_runtime_integration_test.cpp` cases); `voxel_browser`/
  `voxel_browser_server` also rebuilt clean. Not verified under a no-Lua/
  ASan config this session (`build-meshing/` no longer exists on this
  machine, per 6.4's entry above) — the Lua-specific bindings are entirely
  inside `pack_runtime.cpp`'s existing `#if VB_WITH_LUA` region, and the
  `ServerSession`/`WorldReplicator`/`BlockDamageSystem` changes have no Lua
  dependency at all, so the no-Lua stub-build risk is low, just not
  re-confirmed here.

- **2026-09-18 — Phase 6.4 generic per-key persistent storage landed
  (uncommitted).** `vb.db.get(key)`/`vb.db.set(key, value)`/
  `vb.db.delete(key)` + `vb.crypto.hash(data)`, per `ARCHITECTURE_SPEC.md`
  §10.6/§10.7 and `REMAINING_TASKS.md` 6.4.
  **Backend decision, deliberately not SQLite:** the task doc called SQLite
  "the leading candidate" but also explicitly flagged it as "implementation
  detail, not a design blocker." Added `vb::script::ScriptDb`
  (`inc/vb/script/db.hpp` + `src/script/db.cpp`) instead: one file per key,
  content-addressed by `sha256(key)` under a 2-hex-prefix shard dir
  (`<content_pack>/db/<prefix>/<hash>`) — the exact same on-disk shape
  `vb::assetsync::ClientAssetCache` already uses (§4.4, `src/assetsync/
  cache.cpp`), write-to-`.tmp`-then-`rename()` included. Rationale: no new
  dependency (SQLite meant either FetchContent-ing an amalgamation build or
  another package-manager landmine like GNS's protobuf, §5), the spec's own
  scaling complaint was specifically about `vb.storage`'s single JSON blob
  (one record *shared* by every key) — a real filesystem entry per key
  already solves that — and the spec gives `vb.db` no list/enumerate/query
  surface, so nothing here actually needs SQL. If a future feature needs
  range queries, transactions, or listing all keys, swapping the backend is
  a pure `ScriptDb`-internal change; nothing in `PackRuntime`'s Lua bindings
  would need to move.
  **`vb.crypto.hash`**: a from-scratch, dependency-free SHA-256
  (`inc/vb/core/sha256.hpp` + `src/core/sha256.cpp`, `vb::core::sha256_hex`)
  rather than pulling in a crypto library — same "small and self-contained
  beats a new dependency" call as the backend above, and it's also what
  `ScriptDb` uses internally for key-to-filename hashing (one algorithm,
  two call sites). Verified against three NIST/RFC test vectors (empty
  string, `"abc"`, the 56-byte SHA-256 vector) in `sha256_test.cpp` — the
  vectors were pulled from .NET's `SHA256.ComputeHash` run locally via
  PowerShell (cross-checked against Python's `hashlib.sha256`, not
  hand-typed from memory) rather than trusted from recall, since a
  self-authored implementation being checked against a self-typed "known"
  vector proves nothing if the vector itself is wrong.
  **Construction-order trap avoided, not hit:** `PackRuntime::Impl::db`'s
  root path is derived from `storage_path.parent_path()`, so `db` is
  declared *after* `storage_path` in the `Impl` struct (member init order
  follows declaration order, not initializer-list order) and initialized
  from `storage_path` itself in the ctor body, not from the constructor's
  `path` parameter after it's already been `std::move`'d into
  `storage_path` — moving-from-and-then-reading `path` again would've been
  UB-adjacent (unspecified-but-valid state, not guaranteed unchanged).
  **Values round-trip through the same `json_to_lua`/`lua_to_json` helpers
  `vb.storage` already uses** (`pack_runtime.cpp`, anonymous namespace), so
  `vb.db.set(key, {level = 3})` persists a real table, not just strings —
  consistent with `vb.storage`'s existing behavior, unlike a naive
  string-only KV store.
  **Verification:** full `vb_tests` green on `build-net-lua`
  (`VB_WITH_NET=ON`, `VB_WITH_LUA=ON`, 215/215 cases — up from 197 at the
  last count in this file, Phase 6.1's entry above; the delta is this
  session's `sha256_test.cpp` (5 cases) + `script_db_test.cpp` (7 cases) +
  2 new `pack_runtime_test.cpp` cases, plus whatever landed in 6.2/6.3
  between those counts that this entry doesn't re-derive). Not verified
  under a no-Lua/ASan config this session — `build-meshing/` (the pre-built
  ASan dir prior entries used for that) no longer exists on this machine;
  `db.cpp`/`sha256.cpp` don't touch sol2 at all though, so the no-Lua stub
  build risk is low, just not actually re-confirmed here.
  **Not done:** no content uses this yet (same "mechanism before content"
  posture as every other Phase 4/6 primitive) — `content/base` has no login
  flow. `vb.db` has no TTL/expiry and no enumeration, matching the spec
  exactly (get/set/delete by an already-known key only).

- **2026-09-17 — Phase 6.1 entity kinds as classes landed (uncommitted).**
  `vb.world.spawn(kind, pos)` now actually does something: it creates a
  spawned instance whose `self` is a persistent Lua table (`ScriptState` in
  spirit) stored in `PackRuntime::Impl::entities`
  (`src/script/pack_runtime.cpp`), fires `on_spawn(self)` once, then
  `on_tick(self, dt)` once per `dispatch_tick` for as long as it's alive.
  **Design deviation from `ARCHITECTURE_SPEC.md` §7.1/§7.2, deliberate:** the
  spec assumes an EnTT-backed `ScriptState` component driven by real
  `ScriptPreTickSystem`/`ScriptPostTickSystem` classes — neither exists.
  Phase 3.1's generic registry wiring is still deferred (see §2 below), the
  same gap 5.1's `ItemDropSystem` hit first; this item followed that exact
  precedent again rather than building the general system: a small
  hardcoded map, replicated through the interest grid exactly like a dropped
  item (`ServerSession::spawn_script_entity`/`set_script_entity_state`/
  `remove_script_entity`, `src/net/session.cpp`, literally copy-pasted from
  `spawn_item_drop`'s shape). `self`'s base-component accessors
  (`get_pos`/`set_pos`/`get_kind`/`damage`/`remove`) reach it via a shared
  metatable's `__index`, not a usertype — arbitrary fields (`self.hp = 10`)
  live directly on the table, unlike `PlayerHandle` which is rebuilt fresh
  every call and has nowhere to persist anything.
  **`on_hit` has no built-in trigger** — the engine tracks no health at all
  for generic entities (matching 6.5's still-unbuilt "engine takes no
  position" stance on damage). `self:damage(amount, cause)` just fires
  `on_hit` as a notification; a pack wanting mob HP owns that entirely on
  `self` and calls `self:remove(cause)` (which fires `on_death` then
  despawns) itself.
  **Trap that cost a debugging round in the integration test:** don't assert
  an exact `self` field value right after `pump(N)` following a spawn — the
  spawning chat message doesn't land on the server until a tick or two into
  the pump, and every remaining tick in that same `pump` call already runs
  `on_tick` before the assertion, so e.g. an `hp` seeded to `10` and
  incremented by `dt` each tick is already `> 10` by the time you can check
  it, not exactly `10`. Assert a baseline (`> 10`, or capture the value into
  a Lua global) and compare against *that* after more ticks instead of a
  literal.
  **Test-running gotcha hit this session, unrelated to the code:** driving
  MSVC through `vcvars64.bat` from an agent shell via `cmd.exe /c '<path>'`
  silently hangs forever if the path is wrong (e.g. a bash `/tmp/...` path
  that doesn't resolve to the same location under `cmd.exe`) — no error, no
  output, just sits there looking like a slow compile. Write the batch file
  to (and invoke it from) a real Windows path (e.g. the scratchpad dir under
  `C:\Users\...\AppData\Local\...`), not bash's `/tmp`. `cmd /c '"<vcvars>"
  && ninja ...'` chained in one line via the PowerShell tool worked reliably
  where the two-line-batch-file-via-Bash-tool approach didn't.
  **Verification:** full `vb_tests` green on `build-net-lua`
  (`VB_WITH_NET=ON`, `VB_WITH_LUA=ON`, 197/197 cases) and on `build-meshing`
  (`VB_WITH_LUA=OFF`, ASan build, 166/166 cases, no ASan findings — confirms
  the `ServerSession` engine-side change alone, without any Lua bindings,
  compiles and behaves under the no-Lua config too, same as 6.6's
  verification pattern).
  **Existing test updated:** `pack_runtime_test.cpp`'s
  "`vb.world.spawn` on an unregistered kind" case asserted the old stub's
  silent-no-op-returns-nil behavior; changed to assert it now raises a Lua
  error, matching `vb.world.set_block`'s existing "reject, don't silently
  swallow" convention for an unknown id.
  **Not done:** no client-side rendering branches on `EntityKind` yet (same
  pre-existing limitation `world::kItemDropKind` already has — every remote
  entity draws the same placeholder billboard); no automatic despawn-on-
  zero-health since no generic health primitive exists.

- **2026-09-17 — Phase 6.6 player damage/death primitive landed
  (uncommitted).** `player:damage(amount, cause)` (`PlayerHandle::damage`,
  `src/script/pack_runtime.cpp`) → `ServerSession::damage_player`
  (`src/net/session.cpp`) is now the one way to reduce a player's `Health`
  from Lua; the void-kill check is just another call into the same
  `apply_damage()` helper with `cause = "void"`, not a separate code path.
  `check_respawns()` no longer hardcodes the outcome (instant full heal,
  teleport to the join spawn point, a fixed chat line) — it calls a
  `ServerSession::set_respawn_handler` callback (unset = the exact old
  behavior, verified by `netcode_test.cpp`'s pre-existing void-kill test
  passing unmodified) and `PackRuntime::attach_session` wires one in only
  when a pack actually registered `vb.on("player_death", ...)`.
  **Bug caught by the new integration test, not just reasoned about:** the
  first version of `drop_inventory = true` handling spawned the dropped
  items at the handler's *returned* respawn position (`decision.pos`) —
  since the player is teleported to that exact same position in the same
  tick, `ItemDropSystem`'s pickup radius immediately picked the items right
  back up, so the inventory never actually looked empty to the client. Fixed
  by capturing the player's position *before* `check_respawns()` overwrites
  it (i.e. where they actually died) and dropping there instead
  (`run_respawn_handler` in `src/script/pack_runtime.cpp`). Worth
  remembering for any future code that both teleports a player and spawns
  something at "their" position in the same tick — the interest-grid pickup
  systems don't know or care which one happened first.
  **Verification:** full `vb_tests` green on `build-net-lua`
  (`VB_WITH_NET=ON`, `VB_WITH_LUA=ON`, 196/196 cases) and on `build-meshing`
  (`VB_WITH_LUA=OFF`, ASan build, 166/166 cases — confirms the stub-build
  path and the engine-side change alone, without any Lua bindings, both
  still compile and pass).
  **Not done:** no content uses this yet — `content/base` has no
  `death.lua`, so the shipped base pack still gets the built-in fallback
  behavior (same "mechanism before content" posture as every other Phase
  4/5 item). Fall damage, PvP, mob damage, hunger are all still separate,
  unstarted follow-ups (`REMAINING_TASKS.md` 6.6's last bullet) — this item
  only adds the primitive and the decision hook, not any of the systems that
  would call them.

- **2026-09-15 — "Heap corruption after normal block" root-caused to the
  NVIDIA OpenGL driver itself, not this codebase (no code fix; investigation
  closed, uncommitted).** Reported by the user (Windows, `build-net`, MSVC
  Debug config, real `voxel_browser.exe`, NVIDIA GeForce RTX 5060 Laptop GPU,
  driver 32.0.15.9595 / 2026-03-15): Event Viewer showed `voxel_browser.exe`
  faulting in `ucrtbased.dll` with exception `0x80000003`
  (`STATUS_BREAKPOINT` — the Debug CRT's own heap-corruption detector
  hitting `DebugBreak()`), reported as happening after breaking a block.
  **Investigation path, each step confirmed by an actual before/after repro,
  not just code reading** (see `cmake/Sanitizers.cmake` below for the ASan
  build setup this all rode on):
  - Ruled out `VB_WITH_NET`/GNS: reproduces in plain `--singleplayer`
    (`GnsTransport` is never even constructed there).
  - Ruled out a data race in `ChunkMeshWorkerPool` *and* `WorldGenWorkerPool`:
    reproduces identically with **both** forced to `kSynchronous`
    (single-threaded) — a stack trace from that run showed the corruption
    detected inside `main()` itself, single-threaded top to bottom.
  - Ruled out block-editing as such: reproduces from ordinary chunk
    streaming/meshing on join alone, no block needs to be broken — the
    original "after breaking a block" was misleading; breaking blocks just
    triggers a lot of re-meshing (`bump_all_neighbor_revisions`/
    `bump_neighbour` cascades), which surfaces it faster, especially at a
    small `render_distance` where most/all loaded chunks border each other.
  - `mesh_chunk_from_snapshot`'s indexing and `model_from_mesh`'s raylib
    `Mesh` buffer sizes were checked byte-for-byte against the vendored
    raylib 5.5 source (`_deps/raylib-src/src/rmodels.c`) and are correct.
  - **Built a minimal isolated repro** (`--mesh-stress-test` diagnostic mode,
    temporarily added to `voxel_browser.exe` then removed once the finding
    was confirmed — see below to recreate it): a real window, a handful of
    directly-generated/relit chunks fed into a plain `ClientChunkStore` via
    the real `encode_chunk_payload`/`apply_add` wire path (no networking, no
    `WorldGenWorkerPool`, no `WorldReplicator`), then a tight loop of random
    `ClientChunkStore::edit_block()` calls + `ChunkRenderer::sync()`/`draw()`
    every iteration. **Reproduced the exact same ASan failure in under 2000
    iterations with as few as 3 chunks** — proving the bug needs zero
    networking/worldgen/lighting-cascade machinery and lives entirely in
    `ChunkRenderer` + raylib's GPU mesh upload/unload path. With a *single*
    chunk (repeatedly re-edited, re-meshed, re-uploaded 5000×) it never
    reproduced — the trigger is specifically **many distinct chunks'
    GPU meshes cycling through create/destroy**, not raw repetition.
  - **Instrumented `chunk_renderer.cpp` directly** (temporary, removed after
    diagnosis): recorded every uploaded `Mesh`'s buffer pointers
    (`vertices`/`texcoords`/`normals`/`colors`/`indices`) and re-validated
    them immediately before every subsequent `UnloadModel`, and separately
    asserted every field we never set (`tangents`/`texcoords2`/
    `animVertices`/`animNormals`/`boneIds`/`boneWeights`/`boneMatrices`/
    `boneCount`) stays null/zero at three points: right after `Mesh mesh{}`,
    right before `UploadMesh`, and right before `UnloadModel`. **None of
    these ever fired** — every pointer our own C++ code sets or expects to
    stay null was still exactly correct one call away from the crash.
  - **The actual crash, caught with this instrumentation active, was
    `AddressSanitizer: attempting free on address which was not malloc()-ed`
    with the stack trace entirely inside `nvoglv64.dll` (NVIDIA's OpenGL
    driver), on a driver-spawned thread (`T5`) — not `T0` (ours), not any
    thread this codebase creates.** Confirmed independent of `FLAG_MSAA_4X_HINT`
    (still reproduces with MSAA off).
  **Conclusion:** this is heap corruption originating *inside the NVIDIA
  OpenGL driver's own internal memory management*, triggered by rapid
  VAO/VBO create/destroy churn (`UnloadModel`+`UploadMesh` on many distinct
  chunks in quick succession — exactly what `ChunkRenderer::upload()`'s
  full-recreate-every-revision-change approach produces, especially at small
  `render_distance`s where border-neighbour re-mesh cascades touch most of
  the loaded set on almost every edit). ASan's global allocator interception
  catches the driver's own internal `free()` calls process-wide, which is
  how this became visible at all; the plain Debug-CRT crash the user
  originally saw is presumably the same underlying driver-side corruption,
  just caught later/differently by `ucrtbased.dll`'s heap validator instead.
  **This is not a bug in `voxel_browser`'s code** — exhaustive review (this
  session) and instrumentation found nothing wrong in
  `ChunkRenderer`/`model_from_mesh`/`mesh_chunk_from_snapshot`/
  `ClientChunkStore`/`LightEngine`, and the crash's own stack trace is
  entirely inside the vendor driver DLL.
  **Recommended for the user:** update the NVIDIA driver first (32.0.15.9595
  was current as of 2026-03-15 — check for anything newer) as the highest-
  leverage fix; this class of driver-internal VBO-lifecycle bug is the kind
  vendors do fix over time, and no code change here can guarantee avoiding
  it outright.
  **Follow-up hardening — implemented in a later session (2026-09-15,
  `src/render/chunk_renderer.cpp`):** `ChunkRenderer::upload()` used to do a
  full `UnloadModel`+`model_from_mesh`(fresh `UploadMesh`) on *every* revision
  change, for every affected chunk — real VAO/VBO churn on essentially every
  edit/re-mesh. It now over-allocates each chunk's GPU buffers with 25%
  headroom (`capacity_for()`), and a re-mesh whose new vertex/index count
  still fits reuses the existing VAO/VBOs in place via `UpdateMeshBuffer()`
  (`update_gpu_mesh()`) instead of recreating them; only a re-mesh that
  outgrows its current capacity still does a full unload/recreate. Confirmed
  by the user: the crash (this section's whole investigation) has not
  recurred since. Doesn't change the fact that occasional full recreation is
  still necessary (some re-meshes do outgrow capacity), so this reduces the
  odds of hitting the driver bug rather than eliminating the trigger
  outright — the driver-level root cause means no application-level change
  can be a guaranteed fix.
  **Toolchain note, needed for ANY future ASan work on this project on
  MSVC (kept, `cmake/Sanitizers.cmake`):** `VB_ENABLE_ASAN=ON` alone fails to
  link `voxel_browser`/`voxel_browser_server`/`vb_tests` against the
  (non-ASan-instrumented, FetchContent-built) `GameNetworkingSockets` static
  lib with `LNK2038` ("mismatch detected for 'annotate_vector'/
  'annotate_string'/'annotate_optional'"). Fixed by adding
  `_DISABLE_VECTOR_ANNOTATION`/`_DISABLE_STRING_ANNOTATION`/
  `_DISABLE_OPTIONAL_ANNOTATION` compile definitions to `vb_sanitizers` when
  `MSVC AND VB_ENABLE_ASAN` — trades away ASan's container-slack-overflow
  checks (`vec[vec.size()]` while still under `capacity()`) for the ability
  to link against non-instrumented static libs at all; every other ASan
  check (real heap-buffer-overflow, use-after-free, bad-free — everything
  this investigation actually used) is unaffected. Also: `clang_rt.asan_
  dynamic-x86_64.dll` (found under `VC\Tools\MSVC\<ver>\bin\Hostx64\x64\`)
  is not on `PATH` by default and must be copied next to the built `.exe`s
  or the ASan build won't even launch (`error while loading shared
  libraries`).
  **To reproduce the isolated repro again** (e.g. to verify a future driver
  update, or test the buffer-reuse mitigation above): the `--mesh-stress-test`
  diagnostic mode described above was removed from `main.cpp` after use, not
  committed — recreate it from this writeup if needed (real `Window`, a few
  `WorldGenerator`-generated + `LightEngine`-relit chunks fed through
  `encode_chunk_payload`/`ClientChunkStore::apply_add`, then a loop of random
  `edit_block()` + `ChunkRenderer::sync()`/`draw()` calls — reproduced with
  as few as 3 chunks and ~2000 iterations).

- **2026-09-15 — FPS dip while chunk-streaming/moving, fixed
  (`src/world/chunk_mesh_snapshot.cpp`).** Reported by the user after the
  driver-corruption crash above stopped reproducing: still a noticeable FPS
  dip specifically while chunks streamed in during movement, even with
  `ChunkMeshWorkerPool`'s background threads doing the actual meshing.
  Root cause: `build_chunk_mesh_snapshot()` — the per-chunk copy step that
  *must* run on the main thread (it's the only thread allowed to touch
  `ClientChunkStore`, see `chunk_mesh_snapshot.hpp`) — was calling
  `ClientChunkStore::block_at()`/`light_at()` per padded voxel. Each of those
  does a `ChunkCoord` → `Chunk*` unordered_map lookup from scratch, so one
  chunk's snapshot (`kMeshSnapshotVolume` ≈ 39k padded voxels) cost ~78k
  hashmap lookups, done synchronously on the main thread, up to
  `submit_budget` (8) chunks per frame — squarely on the frame already busy
  with movement/streaming. Fixed by resolving each of the (up to) 27
  neighbour chunks once per snapshot via a single `find()` call, then
  indexing straight into each chunk's block/light arrays (O(1)) for every
  voxel instead. Verified byte-identical snapshot output against the
  existing `mesh_chunk()` direct-path test
  (`mesher_test.cpp`'s "matches mesh_chunk"-style cases). User confirmed:
  "buttery smooth" after this fix, in both singleplayer and a real
  server+client multiplayer smoke test (two headless clients joined over
  real GNS/UDP, streamed chunks, and disconnected cleanly with no crash).

- **2026-09-15 — Cross-chunk sky-light bug fixed: false-bright band every 32
  blocks while mining (uncommitted).** Reported by the user: "as I mine
  deeper, on coordinate when vertical displacement is 32, the block suddenly
  becomes bright."
  Root cause: `LightEngine::relight_chunk` (`src/world/lighting.cpp`)
  computed sky light **per chunk in total isolation** — it always seeded full
  brightness at every non-opaque voxel in the chunk's own top layer (local
  y = 31), with no way to know whether another chunk was loaded directly
  above it blocking real sunlight. Both call sites relied on this in
  isolation: `ChunkLifecycleSystem` (on generation) and
  `WorldReplicator::apply_block_edit` (on edit) each called
  `relight_chunk(*chunk)` with no neighbour context at all.
  This generator has **no caves** (`WorldGenerator` fills solid below its
  heightmap unconditionally, `src/worldgen/generator.cpp`), so a chunk fully
  below the natural surface is 100% opaque stone including its own top
  layer — the isolation bug can't actually manifest from generation alone
  with this generator (transmittance is 0 there regardless of what's above).
  The real, common trigger is **mining**: breaking a block at the very top
  of a chunk (world y ≡ 31 mod 32) that's buried under another loaded, solid
  chunk creates an opening exactly where the old code assumed open sky —
  `WorldReplicator::apply_block_edit`'s call to a bare `relight_chunk`
  matches this exactly.
  **Fix:**
  - `LightEngine::relight_chunk(Chunk &chunk, const Chunk *above = nullptr)`
    — `above`'s bottom (local y = 0) sky-light row seeds this chunk's top
    layer instead of assuming open sky, attenuated the same way a normal
    interior BFS step is. `above == nullptr` (the default) keeps the exact
    old unattenuated-kMaxLight behaviour, so every existing direct caller/test
    is unaffected — only actually correct for the topmost chunk in a loaded
    column, which relight_column() (next) accounts for.
  - `relight_column(engine, coord, find, on_relit)` (`lighting.hpp`, header-
    only template): relights `coord` with its real neighbour above, then
    cascades the same relight downward through **every** consecutively
    loaded chunk below it, unconditionally, to the bottom of the loaded
    stack — bumping each chunk's revision iff its light output actually
    changed, and invoking `on_relit(coord, light_before, chunk_after)` once
    per chunk visited so callers can build protocol deltas / track
    newly-ready chunks without lighting.hpp knowing about the wire format.
    **Deliberately unconditional, not early-exit-on-"unchanged"**: a first
    attempt compared each chunk's light output to what it was immediately
    before that same call to decide whether to keep cascading, but a chunk
    that had never been lit before starts all-zero by construction, which is
    indistinguishable from "correctly recomputed to all-zero" (e.g. under a
    solid roof) — the early-exit stopped the cascade before reaching chunks
    that still needed it. Caught by a test that asserted the exact chunks
    visited (`relight_column cascades a solid roof's shadow down through
    every loaded chunk below it`, `lighting_test.cpp`) before trusting the
    logic. Loaded columns are shallow (a handful of chunks per the server's
    vertical view distance) so the extra unconditional relights are cheap.
  - `ChunkLifecycleSystem::update()`'s `ingest()` (`chunk_lifecycle.cpp`):
    now inserts every finished chunk from a batch into `world_` **first**,
    then cascades each via `relight_column` — not relight-then-insert like
    before. Matters because chunk generation order across worker threads has
    no relation to column position; the chunk below can easily finish before
    the chunk above exists yet, and inserting the whole batch before any
    cascading means a same-tick sibling is already visible to `find_chunk`
    when its turn comes.
  - `WorldReplicator::apply_block_edit` (`world_replicator.cpp`): uses
    `relight_column` instead of a bare `relight_chunk` call, and now builds +
    fans out **one delta per chunk the cascade actually touches** (the
    primary edited chunk always carries the block change; any chunk below it
    only carries light changes), each delta sent only to the players who
    mirror that *specific* chunk (not necessarily the same watcher set as
    the primary chunk).
  - `ClientChunkStore::edit_block` (`client_chunk_store.cpp`): same swap, for
    the client's local optimistic prediction. Needed a new non-const
    `Chunk *find(ChunkCoord)` overload (previously const-only) so
    `relight_column`'s lookup callback can hand back a mutable chunk for the
    cascade to relight in place.
  - **New, and probably the least obvious part of this fix:**
    `WorldReplicator` previously only noticed a chunk if it entered or left a
    player's view (`diff_chunk_sets` on presence alone) — never an
    already-visible chunk changing in place. A chunk generated and sent
    *before* its real neighbour above finished loading (arbitrarily many
    ticks later, since worker threads process the queue out of column
    order) gets its light corrected server-side by that neighbour's own
    cascade, but an already-connected player who received the wrong version
    would never see the correction otherwise. Added
    `last_sent_revision_: NetId -> ChunkCoord -> revision` and changed
    `tick()` to compare it against `world_`'s current revision for every
    *visible* chunk (not just newly-entered ones), re-sending a full
    `S2C_ChunkAdd` whenever they differ. `apply_block_edit`'s own delta path
    updates the same map so tick() doesn't redundantly re-send what an edit
    already just delta'd.
  **Scoped out, known follow-up:** no horizontal/diagonal propagation — if a
  cascaded chunk's new light affects a sideways neighbour's border AO, that
  neighbour isn't re-flagged here. Much smaller-magnitude than the
  whole-layer band this fixes; not attempted.
  **Tests:** `lighting_test.cpp` — `relight_chunk` with/without `above`
  (including "straight down through open air, no falloff" and "solid
  `above` stays dark, not falsely sky-lit"), `relight_column` cascade depth
  and the always-report-the-starting-chunk contract, using a small in-memory
  `FakeStore` test harness. `world_replication_test.cpp` — two end-to-end
  tests through the real `World`/`ChunkLifecycleSystem`/`WorldReplicator`
  stack: breaking a block at the top of a chunk buried under another solid,
  loaded chunk stays dark (exercises the real mining-trigger scenario, using
  the fact that any chunk with `top <= world y 31` is guaranteed 100% solid
  stone regardless of seed, since `WorldGenParams{}`'s default
  `base_height`/`amplitude` never puts the real surface below y=36); and a
  focused test of the new `last_sent_revision_` resend path (bump a
  loaded chunk's revision directly, confirm the next `tick()` re-sends it
  with no view change). All new tests confirmed to fail without their
  corresponding fix before being trusted (the flawed early-exit cascade
  design above was caught this way, not just theorized). Full `vb_tests`
  green on both `build/` and `build-net/` (`VB_WITH_NET=ON`, real GNS) — only
  the one pre-existing, unrelated sandbox UDP-bind failure noted elsewhere in
  this file.
  **Not yet done:** not visually verified against a live client (same
  limitation as the meshing-threading entry below — no GL context available
  here); rests on the test suite + code review.

- **2026-09-15 — Near-black flash on predicted block breaks fixed
  (uncommitted).** Reported by the user: "for a split second, I see a black
  block appear on the block to be destroyed before the cell becomes empty."
  Root cause: `ClientChunkStore::edit_block()` (`src/world/client_chunk_
  store.cpp`) only ever mutated the block volume, never the light volume.
  The voxel that just became air keeps whatever light value it had *while it
  was still solid* — typically 0, since a buried block was never reached by
  the sky-light flood. `mesh_chunk`/`mesh_chunk_from_snapshot` read exactly
  that stale value as the "outside" light for the newly-exposed neighbouring
  face (`light_at(outside)` in the mesher), so the face right where the
  block used to be renders at ~15% brightness (`base_light` floor from the
  `* 0.85 + 0.15` formula) — reads as a near-black block — until the server's
  authoritative `S2C_ChunkDelta` (which *does* carry a relit value; the
  server calls the same `relight_chunk` on every edit, per 5.2) arrives and
  overwrites it. This bug predates the threading work above and isn't caused
  by it, but the extra pool round-trip that change added between the edit
  landing and its mesh being collected likely made the flash last longer /
  more consistently land on an actually-rendered frame instead of being
  skipped over within the same tick.
  **Fix:** `edit_block()` now calls `LightEngine(registry_).relight_chunk(chunk)`
  immediately after a successful `chunk.set()`, mirroring what the server
  already does for the same edit. Because it's the exact same per-chunk
  algorithm run against the same (now-identical) block data, the predicted
  light typically matches the eventual authoritative delta exactly, not just
  "less stale" — no correction pop, not just a shorter flash. Doesn't change
  border-neighbour revision bumping (unaffected, still correct).
  **Known limitation, unchanged by this fix:** `relight_chunk` is
  per-chunk-only (assumes open sky directly above *this* chunk, no
  cross-chunk propagation) — same limitation the server's own relight has
  (`REMAINING_TASKS.md` 2.3/5.2 already track cross-chunk relight as
  separately TODO). An edit near a chunk boundary where the correct light
  actually depends on a neighbouring chunk's shadow can still show a brief
  mismatch until the server's delta corrects it; this fix only closes the
  common buried-block-with-no-cross-chunk-dependency case, which is what was
  reported.
  **Test** (`tests/unit/mesher_test.cpp`, "edit_block relights immediately"):
  a fully-solid, never-relit chunk (light stays at its all-zero default, the
  worst case) with one top-layer block broken — asserts the light there is
  full sky brightness (`kMaxLight`) immediately after `edit_block()`, not the
  stale 0 the bug would leave. Verified failing without the fix (the assert
  is exactly what the old code couldn't produce, since it never touched
  light at all). Full `vb_tests` green on both `build/` and `build-net/`
  (same one pre-existing sandbox UDP-bind failure noted above, unrelated).

- **2026-09-15 — Chunk meshing moved off the main thread (uncommitted)**,
  fixing framerate drops while chunks stream in. Reported by the user: the
  client stutters when receiving new chunks, suspected single-threaded
  meshing (`REMAINING_TASKS.md` 2.5 already flagged "mesh worker pool" as a
  follow-up).
  Root cause confirmed before touching anything: `ChunkRenderer::sync()`
  (`src/render/chunk_renderer.cpp`) ran `mesh_chunk()` (CPU face-culling + AO,
  the expensive part) and the GPU upload back-to-back on the main/render
  thread, budgeted to 8 chunks/frame (`src/client/main.cpp`) — a burst (join,
  teleport, or the still-unpaced `WorldReplicator` burst noted below) pays
  that full CPU cost synchronously across however many frames it takes to
  drain the budget.
  **Fix, split across new files, all in `vb/world` (not `vb_render`) so
  they're unit-testable without raylib/GL:**
  - `inc/vb/world/chunk_mesh_snapshot.hpp` + `.cpp`: `ChunkMeshSnapshot` — a
    copy of one chunk's voxels/light plus a **1-voxel border shell** from its
    neighbours (proved sufficient, not just assumed: `mesh_chunk`'s AO corner
    samples add at most one tangent-axis offset on top of the face normal's
    one-voxel offset, and normal/tangent axes are always distinct, so no
    sample ever reaches 2 voxels out on any single axis). `build_chunk_mesh_
    snapshot(store, coord)` copies it out (must run on the store's own
    thread); `mesh_chunk_from_snapshot(snapshot, registry)` is the actual
    face-culling/AO algorithm, moved here verbatim from the old
    `chunk_mesher.cpp`, now a pure function of the snapshot with zero store
    access — safe on any thread. `chunk_mesher.cpp`'s `mesh_chunk(store,
    coord)` is now a 2-line wrapper (`build` + `mesh_from_snapshot`), kept
    byte-identical in signature/behaviour so every existing `mesher_test.cpp`
    case (and this session's new "meshing from a snapshot matches meshing
    straight off the store" case, which diffs the two paths' `MeshData`
    directly) passes unchanged.
  - `inc/vb/world/chunk_mesh_worker_pool.hpp` + `.cpp`: `ChunkMeshWorkerPool`,
    deliberately mirroring `WorldGenWorkerPool`'s shape (mutex+CV queue,
    dedup-by-coord, `poll_completed()`, `kSynchronous` for tests) since that
    pattern was already established and tested in this codebase. One
    difference from `WorldGenWorkerPool`'s default thread count
    (`hw_concurrency - 1`): this pool defaults to **half** of
    `hardware_concurrency` — `--singleplayer` runs both pools in the same
    process, both bursts tend to correlate (both triggered by player
    movement), so an `hw-1` default here would oversubscribe every core
    ~2x on top of main/render/net. Also added `in_flight_or_queued(coord)` (not
    on `WorldGenWorkerPool`) so `ChunkRenderer` can skip building a snapshot
    for a chunk that's already got a job in flight, instead of wasting the
    copy only to have `submit()` reject it.
  - `ChunkRenderer::sync()` (`chunk_renderer.{hpp,cpp}`) restructured into
    submit/collect halves: drain `pool_.poll_completed()` and GPU-upload each
    result **unless the chunk's live revision has moved past the result's
    `revision`** (a neighbour or the chunk itself changed again after the
    snapshot was taken — discard silently, it self-heals: the coord is no
    longer in-flight once the stale result is polled, so the submit loop
    below requeues it against the current revision next call); then submit
    up to `submit_budget` (still 8, same call site in `main.cpp`, unchanged
    signature) new/changed chunks, skipping any `in_flight_or_queued`. GPU
    upload itself (`model_from_mesh` — `MemAlloc` + `UploadMesh`) is
    unavoidably still main-thread (raylib/GL requirement) but is the cheap
    half of the old per-chunk cost, not the one causing the stutter.
  **Why a snapshot copy and not a live store reference on the worker
  thread:** `ClientChunkStore` is mutated by `apply_add`/`apply_delta`/
  `apply_remove` as net messages decode, on the same thread that would need
  to hand work to the pool — handing a worker a live `ClientChunkStore&`
  while that continues is a data race. The padded-shell copy
  (34×34×34 = 39,304 `block_at`/`light_at` calls, once, on the main thread)
  is comparatively cheap: the *old* `mesh_chunk` already made far more such
  calls per chunk internally (up to 78 world-space lookups per solid voxel,
  each going through `ClientChunkStore`'s coordinate→chunk hashmap), so the
  snapshot's dense single pass is a net win even before counting that the
  actual AO/culling work moves off-thread entirely.
  **Tests** (`tests/unit/mesher_test.cpp`): snapshot-vs-live-store equality,
  unloaded-chunk snapshot behaviour, worker-pool dedup (see the note in that
  test about why it uses a full solid chunk as the target job rather than a
  single block — a trivial job races the OS waking the worker thread within
  the same handful of nanoseconds as the immediate duplicate-submit call on
  the main thread; this was caught as an actual flaky failure while writing
  the test, not theorized), `kSynchronous` mode, and distinct-coord
  concurrent drain. Full `vb_tests` green on both `build/` (`VB_WITH_NET=OFF`)
  and `build-net/` (`VB_WITH_NET=ON`, real GNS) — the only failure on
  `build-net` is the pre-existing sandbox UDP-bind one noted below, confirmed
  unrelated (same failure, same test, on unmodified HEAD).
  **Not yet done / worth knowing before touching this again:**
  - **Not visually verified against the actual reported stutter** — no
    before/after frame-time capture exists yet (`ChunkRenderer` isn't
    unit-testable without a real GL context, so this rests on the reasoning
    above + the passing test suite, not an eyeballed FPS counter). If the
    stutter persists after this, the next suspect is GPU upload cost itself
    (`model_from_mesh`'s `MemAlloc`/`UploadMesh` calls, still synchronous and
    unbudgeted per collected result each frame) or `submit_budget=8` still
    being too high for the per-snapshot copy cost on a slower machine — try
    capping collected uploads per frame too, not just submissions.
  - Doesn't touch the unpaced `WorldReplicator` burst noted below — that's
    server→client wire volume, orthogonal to this (client-side CPU meshing).
  - Cellulose's `greedy_mesh` was later wired into exactly this seam
    (`mesh_chunk_from_snapshot`'s call site inside `ChunkMeshWorkerPool::
    mesh`) and then reverted — its more volatile per-edit vertex/index
    counts defeated this entry's headroom/`UpdateMeshBuffer()` mitigation
    and reproduced the driver crash it exists to avoid. See §8's 15th entry
    (2026-09-16) for the full story; `VB_WITH_MESHING` is unused.

- **2026-09-15 — ACTUAL root cause of the invisible-but-walkable chunk gap
  found and fixed: GNS's default 512 KiB send buffer, silently overflowed.**
  Follow-up report after 2026-09-11's investigation below: "the gap is still
  there" + new evidence — **the invisible blocks can't be broken either**.
  That second fact was the key: `raycast_voxel()` (client `main.cpp`) queries
  `ClientChunkStore::solid_at()`, which is a *pure local read* — it never
  touches the mesher. If breaking also fails, the client's own replicated
  chunk data is missing/wrong at that spot, not just its mesh — ruling out
  every mesh/AO-only theory from the 09-11 pass in one shot and pointing
  straight at chunk streaming (encode → send → receive → decode) instead of
  meshing.
  Reread every step of that pipeline; `WorldReplicator::tick()`,
  `chunk_codec.cpp`'s encode/decode, and `ClientChunkStore::apply_add` all
  checked out (and 09-11 already added loud `VB_ERROR` logging to the decode/
  apply and the `find_chunk()`-returned-null paths — if either fired, the
  report would have said so). That left the one hop with **zero error
  handling at all**: `GnsTransport::send()` (`src/net/gns_transport.cpp`)
  called `SendMessageToConnection(...)` and discarded its return value
  outright — not even captured into a variable. GNS's own header
  (`steamnetworkingtypes.h`) documents exactly this failure mode:
  `k_ESteamNetworkingConfig_SendBufferSize` (upper limit of buffered pending
  bytes) **defaults to 512 KiB (524288 bytes)**, and once hit,
  `SendMessageToConnection` returns `k_EResultLimitExceeded` **instead of
  queuing the message** — reliable-lane or not, a message GNS never accepted
  is never retried by GNS's own reliability machinery, since that machinery
  only covers messages it already has.
  **Measured, not assumed:** wrote a throwaway probe (real `WorldGenerator`,
  seed 1, `encode_chunk_payload` over the *actual default production view
  box* — `view_distance=8`/`vertical_view=3` from `server.toml.example`, i.e.
  chunks_in_view = 17×17×7 = **2023 chunks**, matching the original report's
  "~1811 chunks loaded" order of magnitude) and summed the encoded chunk
  payload bytes: **545,300 bytes — already past the 524,288-byte default
  limit from chunk data alone**, before the per-message protocol framing
  overhead (`frame_message`'s envelope/length-prefix) or any entity-snapshot
  traffic sharing the same connection is even counted. The whole box streams
  in one synchronous `WorldReplicator::tick()` → `ServerSession::
  broadcast_world()` → `send_frames()` burst on join (nothing paces it across
  ticks), so the buffer fills almost immediately after join, mid-burst.
  Whichever chunks fall after the fill point in `chunks_in_view`'s iteration
  order (`for dy { for dz { for dx {...} } }`, y outermost) land as a
  **contiguous rectangular run of coordinates** — exactly "a big rectangular
  region" — and are silently never queued. `WorldReplicator::last_sent_`
  already recorded them as sent (it has no way to know `send()` failed), so
  they're never retried; the server's authoritative `World` has them
  (walkable, matches physics reading `World` directly); the client's
  `ClientChunkStore` never received them (invisible *and* unbreakable, matches
  both raycast and mesher reading the same missing data); deterministic
  per-session because the same view box streams in the same order every join
  (explains "persists across relog" *and* "a fresh client reproduces it too"
  from 09-11 — every fresh join hits the identical buffer-fill point).
  Also explains why no test caught it: every existing chunk-streaming test
  uses `LoopbackNetwork` (in-process `std::vector` queue, no byte-budget
  concept at all) — this is a `GnsTransport`-only failure mode, and
  `VB_WITH_NET` real-network coverage is exactly the thin/uneven part of the
  test suite noted in §3.
  **Fix** (`src/net/gns_transport.cpp`): (1) raise
  `k_ESteamNetworkingConfig_SendBufferSize` to 32 MiB via
  `SteamNetworkingUtils()->SetGlobalConfigValueInt32(...)` once in
  `GnsRuntime::acquire()`, process-wide, before any listen/connect — gives a
  full default view box (and meaningfully larger ones) comfortable headroom;
  (2) `GnsTransport::send()` now captures `SendMessageToConnection`'s
  `EResult` and `VB_ERROR`-logs a failure instead of discarding it, so if a
  future world/view-distance config *does* exceed even the raised buffer,
  it's loud instead of silent, consistent with 09-11's decode/apply logging.
  **Not done, worth doing if view distances grow much further:** this raises
  the ceiling, it doesn't add real backpressure — an unbounded view distance
  or a much larger world could still overflow even 32 MiB in one synchronous
  burst. A proper fix would pace `WorldReplicator`'s initial `diff.entered`
  burst across multiple ticks (a per-connection byte budget per tick) instead
  of relying on a bigger fixed buffer; not done here since the measured
  numbers show the raised buffer comfortably covers the shipped default
  config with a large margin.
  **Verification:** full `vb_tests` suite green on both `build/`
  (`VB_WITH_NET=OFF`, 125/125 cases) and `build-net/`
  (`VB_WITH_NET=ON`, real GNS linked, 124/125 — the 1 failure,
  `GnsTransport: connect, exchange a message, and disconnect over real UDP`,
  is a pre-existing local-sandbox UDP-bind limitation, confirmed by
  reproducing it identically on unmodified HEAD before this fix). Smoke tests
  (`server_smoke`/`client_smoke`/`singleplayer_smoke`) pass on both configs.
  **Not yet verified against a live repro** — the original report was a real
  dedicated server + separate client session; this fix rests on the measured
  byte-count + GNS's documented buffer-overflow behaviour, not a captured
  `SendMessageToConnection failed (result=...)` log line from an actual
  overflow. If the gap somehow recurs after this, the new logging in
  `GnsTransport::send()` will name the exact `EResult` and message size —
  check the server's stderr around join time first.

- **2026-09-11 — investigated "big rectangular gap, doesn't render but I can
  walk on it" (dedicated server + separate client, ~1811 chunks loaded,
  persists across relog).** This turned out to be an incomplete fix — see the
  2026-09-15 entry above for the actual root cause found later. What I
  confirmed from the report + code at the time:
  - Walkable ⇒ the *server's* authoritative `World` genuinely has solid block
    data there (physics reads directly from it). Persists across a full
    client restart ⇒ it's not a client-side stale-mesh-cache issue (a fresh
    `ClientChunkStore`/`ChunkRenderer` reproduces it too). So the bug is
    either (a) something server-side deterministically not sending/encoding
    that chunk's data correctly for this client, or (b) `mesh_chunk`
    deterministically producing an empty/wrong mesh from otherwise-correct
    data.
  - Ruled out empirically, not just by inspection: the `kMaxMeshVertices`
    65532 cap added for the water-culling fix (chunk_mesher.cpp) was the
    prime suspect, but a probe across 40 seeds of real `WorldGenerator`
    terrain (base_height=64, amplitude=28, sea level 62, 3×3×5 chunk
    neighbourhoods so `mesh_chunk` has full context) found a worst case of
    only ~6260 vertices — nowhere near the cap. Smooth heightmap terrain
    structurally can't produce enough exposed faces per chunk to hit it.
  - Ruled out by code reading: `ChunkLifecycleSystem::update()`'s `ingest()`
    silently discards a `pool_.poll_completed()` chunk if it's no longer in
    `wanted` (player moved away between request and completion) — a real
    wasted-work bug, but it can't produce a *permanent* gap for a chunk the
    player is currently standing on/in view of, since the very next
    `update()` call re-requests it (not walkable if the data were truly
    missing anyway — see above). Left unfixed; not the reported symptom.
  - Ruled out by code reading: `WorldReplicator::forget_player()` is called
    on leave (session.cpp), so `last_sent_` doesn't go stale across a
    reconnect with a reused `NetId`. `kWorld` lane (chunk add/delta/remove)
    is `kReliableOrdered`, so a plain dropped UDP packet can't be the cause
    either — GNS retransmits.
  - Ruled out by code reading: `PalettedChunkStore`/`chunk_codec.cpp`
    round-trip is self-consistent by construction — `get()` can only ever
    return a value present in `palette()`, so the encoder's `index_in()`
    can't silently mis-encode a block as palette index 0. No corruption path
    found there.
  - **Found and fixed (real bug, but unconfirmed as THE cause):**
    `ClientChunkStore::bump_all_neighbor_revisions()` only bumped the 6
    face-adjacent chunks. AO samples 3 voxels diagonally around each face
    corner (chunk_mesher.cpp's `a`/`bpt`/`d`), which for a voxel on a chunk's
    edge/corner lands in an edge- or corner-adjacent chunk — one of the other
    20 in the 26-neighbourhood. Those chunks arriving/changing/leaving never
    triggered a re-mesh, leaving AO permanently stale at chunk
    edges/corners. Fixed by bumping the full 26-neighbourhood instead of just
    the 6 faces. This only affects per-vertex *lighting*, not face culling —
    it cannot by itself make a chunk render as fully empty, so it's very
    unlikely to be the reported bug, but it's a real fix regardless
    (regression test: "apply_add bumps a diagonally-adjacent neighbour's
    revision too" in mesher_test.cpp, verified to fail without the fix).
  - **Found and fixed (real code-quality bug, most promising lead for next
    repro):** `ClientSession::apply_gameplay_frame()` discarded the
    `Result<void, ProtocolError>` from `chunks_.apply_add()`/`apply_delta()`
    with `(void)` — any decode/apply failure was completely silent. If a
    chunk *does* ever fail to decode/apply for some data-dependent reason I
    haven't found, this is exactly how it'd manifest: never rendered (mesher
    sees it as unloaded), never retried (server's `last_sent_` already marks
    it sent), zero trace. Now logs via `VB_ERROR("net", ...)` with the coord
    and the `ProtocolError` message on both a malformed frame and a rejected
    apply. **If this bug recurs, check the client's stderr/log output for a
    `chunk (...) add rejected: ...` or `malformed S2C_ChunkAdd: ...` line
    around the time/place it happens** — that will point straight at the
    actual cause (or rule out this whole class of failure if nothing logs,
    which would point back toward the server not sending it at all — check
    `WorldReplicator::tick()`'s per-player diff.entered / `world_.find_chunk`
    lookup next).
  - **Also found and fixed the same session** (the "not yet checked" item
    below was stale — this was actually done here too, just not called out
    in this writeup at the time): `WorldReplicator::tick()`'s `diff.entered`
    loop silently `continue`d if `world_.find_chunk(c)` returned null for a
    chunk `visible` said was loaded moments earlier — "should be unreachable
    single-threaded" but was silent and would wedge the coord into
    `last_sent_` as sent when it wasn't. Now `VB_ERROR`-logs and drops it
    from `visible` so it retries next tick. (Neither this nor the client-side
    logging above ever fired on the 2026-09-15 repro — see that entry above
    for what the actual cause was: a transport-layer silent failure neither
    of these two code paths could see, since the message never even reached
    `find_chunk`/decode on either side.)

- **2026-09-10 — Phase 0 restructure** (uncommitted). Modular CMake
  (`vb_core`/`vb_render`/`voxel_browser`/`voxel_browser_server`/`vb_tests`),
  `cmake/{Dependencies,Warnings,Sanitizers}.cmake` + `cmake/version.hpp.in` +
  `cmake/lua/CMakeLists.txt`, runnable client/server skeletons with `--headless`,
  generated version header, CI updated (multi-binary staging, ctest on all 3 OSes,
  `upload-artifact` v7→v4 typo, lint via `find`, `git describe` no-tag fallback),
  `docs/{protocol,lua-api}.md` stubs, `.clang-format` → c++20, `PROJECT_NAME` →
  `voxel_browser`. Build-blocking bugs §1.1–1.3 fixed; §1.4 worked around.
  Decisions locked: sol2 (§19 Q1), doctest, LZ4, explicit source lists.

- **2026-09-10 — Phase 1.1/1.2/1.3 (partial)** (uncommitted). `vb_core`:
  `result.hpp` (`Result<T,E>`/`Status`), `error.hpp` (`CoreError`/`ProtocolError`
  + total `message()` switches), `math.hpp`, `ids.hpp`, `log.{hpp,cpp}`.
  `vb/protocol/`: `byte_buffer.hpp` (`ByteReader`/`ByteWriter` + LEB128 varints,
  error-accumulating), `message.hpp` (envelope, `MessageType`, `lane_for`),
  `handshake.hpp`+`handshake.cpp` (7 handshake message structs, encode/decode).
  18 unit test cases / 113 assertions, all green under `-Werror`. `docs/protocol.md`
  filled in. **Still open in Phase 1:** TOML config loader, GNS transport,
  connection FSMs, librg spike, client shell (camera/overlay).

- **2026-09-10 — Phase 1.2/1.3 transport + handshake** (uncommitted).
  `vb/net/`: `transport.hpp` (`Transport` interface, `TransportEvent`,
  `lane_for`/`send_mode_for_lane`, `send_message` helper), `loopback.{hpp,cpp}`
  (`LoopbackNetwork` in-process backend), `handshake.{hpp,cpp}` (`ServerHandshake`
  + `ClientHandshake` FSMs with host callback hooks). `NetError` enum added to
  `core/error.hpp`. `tests/unit/net_test.cpp` drives the whole handshake over
  loopback (26 cases / 185 assertions total, green under `-Werror`).
- **2026-09-10 — Phase 1 session layer + integrated singleplayer** (uncommitted).
  `vb/net/session.{hpp,cpp}`: `ServerSession` (per-conn handshake driver,
  timeouts, monotonic net-id alloc, `take_joins`/`take_leaves`), `ClientSession`.
  `vb/net/integrated.{hpp,cpp}`: `IntegratedGame` — loopback server+client in one
  object. `voxel_browser --singleplayer` runs it and completes the join
  (headless too); `singleplayer_smoke` CTest. `ServerHandshake::grant()` exposes
  the `JoinGrant`. `IntegratedGame` needs ~4 `tick()`s to settle (server-polls-
  then-client-polls each tick = one message hop per tick).
  **Next:** `GnsTransport` behind `VB_WITH_NET`.

- **2026-09-11 — Fixed spawn Y: was embedded in terrain by construction for
  ~half of all seeds** (uncommitted). Follow-up report after the fall-through
  fix below: "the player stuck inside blocks on spawn" -- **no falling
  involved this time**, a separate bug. Root cause: `JoinGrant::spawn_pos`
  defaults to a fixed `{0, 64, 0}` (`inc/vb/net/handshake.hpp`), and neither
  `--singleplayer` nor the dedicated server ever overrode it with a real
  terrain height -- singleplayer's hardcoded seed 7 happens to have
  `surface_height(0,0) == 56` (safely below 64, purely by luck), but the
  standalone server now picks a **random** seed by default
  (`server/main.cpp`'s `random_seed()`), and `base_height=64` /
  `amplitude=28` means the real surface ranges roughly `[36, 92]` — probed 30
  seeds, **19 of 30 (63%)** had `surface_height(0,0) >= 64`, i.e. the fixed
  spawn point was at or below ground. Seed 1 is dramatic: surface height 78,
  fourteen blocks above where the player would spawn — fully entombed in
  stone from the instant they joined, zero fall.
  Fixed by adding `worldgen::default_spawn_position(generator, x, z)`
  (`vb/worldgen/generator.{hpp,cpp}`) — feet one voxel above the real
  `surface_height` at the spawn column — and wiring it into a
  `HandshakeServerHost::on_ready` in both `--singleplayer` (`sp_server_host()`
  in client `main.cpp`) and the dedicated server (`server/main.cpp`), neither
  of which supplied a custom host before. `ServerSession`'s constructor
  already fills in `net_id`/`world_seed` on top of whatever `on_ready`
  returns (existing wrapper logic, unchanged) — the host only needs to set
  `spawn_pos`. Tests: `tests/unit/worldgen_test.cpp` — asserts the computed
  spawn sits exactly one voxel above `surface_height` for 3 seeds (including
  1, the dramatic case) and for a non-origin spawn column.
  **This is a different bug from the one below and doesn't supersede it** —
  a correct spawn *point* still needs the *chunk at that point* to be loaded
  before physics starts touching it; both fixes matter together. Not
  discovered together because singleplayer's fixed seed 7 masked this one
  during all of Phase 3-5's development and testing.

- **2026-09-11 — Join-time fall-through-world / embedding bug fixed**
  (`69ea41c`). Reported: "when the player joins, the player immediately
  falls outside the world first, which leads to player get embedded inside
  the terrain." Root cause: physics is entirely input-driven
  (`ServerSession::handle_input_batch` only runs `step_movement` when a
  `C2SInputBatch` arrives) and starts the instant the client sends its first
  input, which can easily be *before* the spawn chunk's async worldgen has
  finished (`WorldGenWorkerPool` runs on real background threads outside
  `kSynchronous` mode — `--singleplayer`'s pool included). An unloaded chunk's
  `solid_at`/`has_chunk` reads as plain air, so the player free-falls with
  **zero collision** for however many ticks generation takes; swept collision
  only ever prevents *new* penetration during a move, it never resolves a
  pre-existing one, so once the chunk finally loads with the player's Y
  already below the real surface, they're just stuck inside solid terrain —
  nothing ever pushes them back out.
  Fix: `physics::ground_area_loaded(feet, has_chunk)` (new, header-only
  template in `movement.hpp` so it works against both `World::has_chunk` and
  `ClientChunkStore::has` without a shared interface) checks the player's own
  chunk plus two below it; `ServerSession::handle_input_batch` and
  `ClientSession::push_input` both skip `step_movement` entirely (freezing
  position/velocity, not just zeroing gravity) whenever it's false. The
  server's freeze is what actually matters for correctness (it's
  authoritative); the client-side one is belt-and-suspenders so the local
  camera doesn't show a premature fall before the first snapshot arrives —
  verified this doesn't fight `reconcile()` (unguarded on purpose: replaying
  unacked inputs against *current*, likely-by-then-loaded chunk data is
  correct) by working through why `netcode_test.cpp`'s fly-mode tests (no
  `WorldReplicator` attached at all, so `chunks_` never receives any chunks)
  still pass: `reconcile()` snaps `predicted_` straight to the server's
  authoritative position every tick regardless of what `push_input` did
  locally, so the client-side freeze only ever affects the sub-tick gap
  before the next snapshot, never overall convergence.
  Tests: `tests/unit/physics_test.cpp` — 5 direct cases against
  `ground_area_loaded()` (nothing loaded, own chunk only, own+2 below, one
  gap in the middle, negative-Y `floor_div` correctness).
  **Known test-coverage gap, stated plainly:** there is *no* automated
  integration test reproducing the actual async join race end-to-end.
  `WorldGenWorkerPool::kSynchronous` (used by most existing integration
  tests) generates instantly inside `submit()`, so it structurally cannot
  exhibit the gap this bug lived in; the real async pool's timing isn't
  controllable enough for a deterministic CI test without adding test-only
  hooks, which felt like over-engineering for this fix. Confidence rests on
  the unit-tested decision function + code-inspection of the two call sites,
  not an end-to-end repro — say so if a future change to the join sequence
  needs that confirmed differently.

- **2026-09-11 — Chunk-neighbour remesh bug fixed** (`c3393be`). Reported by
  the user: "very minor AO error, perhaps arises from receiving data from the
  server" + "square chunks visible under water." Root cause:
  `ChunkRenderer::sync()` only re-`mesh_chunk()`s a chunk when
  `chunk->revision()` changes (chunk_renderer.cpp) — `mesh_chunk()` itself is
  a pure function of current store state and was always correct, but **three
  of the four `ClientChunkStore` mutators never bumped a *neighbour's*
  revision**, only `edit_block()` (the client's own optimistic edits) did:
  - `apply_add` — a chunk arriving after an already-loaded neighbour left
    that neighbour's border permanently meshed as if this chunk were still
    unloaded (air) — wrong culling *and* wrong AO along that seam, forever,
    unless something else happened to touch the neighbour later. Order-
    dependent on server streaming order — explains the "square chunks" report
    exactly (water chunks are flat/homogeneous, so an unculled border face is
    a very visible full 32×32 quad) and is entirely about *timing of data
    arriving from the server*, matching the other half of the report too.
  - `apply_delta` — the authoritative block-edit broadcast (anyone's edit,
    not just yours) never bumped the neighbour whose culling/AO depends on
    a border voxel it just changed.
  - `apply_remove` — a chunk unloading never told the neighbour that had been
    culling a face against it to re-expose that face.
  Fixed by extracting `bump_all_neighbor_revisions(coord)` (bump all 6
  face-adjacent loaded neighbours, unconditionally) and calling it from all
  three; `edit_block()` keeps its own tighter border-only variant since it
  only ever touches one voxel and knows exactly which single neighbour (if
  any) is affected. Regression tests in `tests/unit/mesher_test.cpp` assert
  the neighbour's revision actually changes (not just that `mesh_chunk()`'s
  output would be correct if called again — the bug was entirely about
  *whether* it gets called again) — verified they fail without the fix
  (reverted the source, reran, confirmed all 3 fail with `1 > 1`) before
  trusting them.
  **Lesson for next time a chunk-border bug shows up:** any code path that
  changes what a chunk looks like from the outside (new data, an edit
  anywhere in it, unloading) needs to bump *every loaded neighbour's*
  revision, not just its own — the renderer has no other signal to re-mesh a
  chunk whose own data didn't change but whose correct mesh output did.

- **2026-09-11 — macOS Homebrew protobuf configure crash fixed** (`f86aeec`).
  Reported by the user building `-DVB_WITH_NET=ON` locally on macOS with
  `brew install protobuf`:
  ```
  CMake Error at .../protobuf-targets.cmake:42 (message):
    Some (but not all) targets in this export set were already defined.
    Targets Defined: protobuf::libprotobuf-lite, protobuf::libprotobuf, ...
    Targets not yet defined: protobuf::libupb, protobuf::protoc-gen-upb, ...
  ```
  Root cause: `cmake/Dependencies.cmake` called `find_package(Protobuf
  REQUIRED)` itself (added purely so a missing protobuf would fail with a
  clearer message) *and then* `FetchContent_MakeAvailable(gamenetworkingsockets)`
  triggered GNS's own `src/CMakeLists.txt:12 find_package(Protobuf REQUIRED)`
  — two `find_package(Protobuf)` calls in one configure run. Homebrew's
  generated `protobuf-targets.cmake` has a fatal-error guard against exactly
  this (the two calls end up requesting slightly different component sets, so
  the second sees a partially-already-defined target list and refuses rather
  than silently re-defining). vcpkg's protobuf on Windows didn't trip the same
  guard, which is why this wasn't caught during Phase 1.2's own local
  verification (Windows-only, see below) — **a dependency behaving on one
  platform's package manager doesn't mean it behaves on another's; the
  `VB_WITH_NET` local verification story only ever covered Windows before this
  bug report.** Fix: deleted our own `find_package(Protobuf REQUIRED)` call
  entirely; GNS's internal one is now the sole call and already produces a
  clear "Could NOT find Protobuf" error on its own if it's missing, so nothing
  was lost. **Do not add `find_package(Protobuf ...)` back to
  `Dependencies.cmake`** — see §3 and §5 above.
  Verified only that the *configure* step now succeeds on Windows with the fix
  in place (reconfigured with the vcpkg toolchain file, same as Phase 1.2's
  original verification) — the macOS report itself is unconfirmed-fixed
  pending the user re-running it; if it still fails on macOS after this, the
  next suspect is the OpenSSL linkage (`brew install openssl` — macOS uses
  GNS's default OpenSSL crypto backend, not BCrypt), not protobuf.

- **2026-09-11 — Phase 1.2: `GnsTransport`, real UDP networking** (`cc2241a`).
  `vb/net/gns_transport.{hpp,cpp}` — a real `Transport` backend over
  GameNetworkingSockets, same "always-present header, `kBackendUnavailable`
  stub without the flag" pattern as `vb::script::Vm`. One process-wide
  `GnsRuntime` (refcounted `GameNetworkingSockets_Init`/`_Kill`; GNS exposes
  exactly ONE global connection-status callback for the whole process, not
  one per interface) routes each status-change event to the owning
  `GnsTransport` via listen-socket/connection-handle registries — needed so a
  server + several clients can share one process (tests) without cross-talk.
  `voxel_browser_server` now actually runs a game (`World` +
  `WorldGenWorkerPool` + `ServerSession` + `WorldReplicator` over a real
  listen socket) instead of an empty sleep loop. `voxel_browser`'s
  non-singleplayer path connects for real (`RemoteConnection` in `main.cpp`);
  `main.cpp` was refactored to drive singleplayer and remote play through one
  `ClientSession*` instead of duplicating the per-frame logic.
  `tests/unit/gns_transport_test.cpp`: real UDP connect/send/disconnect over
  127.0.0.1 (`#if VB_WITH_NET`, mirrors the `vb::script::Vm` test-gating style).

  **This was the single hardest dependency in the project so far — details
  matter if it needs touching again:**
  - **Protobuf cannot be `FetchContent`-ed for this.** GameNetworkingSockets'
    CMakeLists does a bare `find_package(Protobuf REQUIRED)`. Protobuf's own
    CMake only *generates* a discoverable config (`protobuf-config.cmake`) as
    part of an `install()` step, and its library doesn't exist yet at
    FetchContent's configure time either way — there is no clean way to
    satisfy `find_package(Protobuf)` from a bare source checkout. Protobuf
    must come from a real package manager: **vcpkg on Windows**
    (`vcpkg install protobuf:x64-windows` +
    `-DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake` — this
    box now has vcpkg cloned+bootstrapped at `D:\vcpkg`), **apt on Linux**
    (`libprotobuf-dev protobuf-compiler`), **brew on macOS** (`protobuf`).
    `cmake/Dependencies.cmake` has the full write-up.
  - **sol2's pattern does not repeat here**: unlike sol2 (bump the pin, done),
    this is a "the dependency's own build system requires a pre-installed
    sub-dependency" problem — no version bump fixes it.
  - **GNS pin bumped v1.4.1 → v1.6.0**: v1.4.1 doesn't compile at all under a
    strict/modern stdlib — `std::string_view::c_str()` (not a real API) in
    `csteamnetworkingsockets.cpp` and two other files. Fixed upstream by v1.6.0.
  - **ICE/WebRTC disabled** (`ENABLE_ICE=OFF`, `USE_STEAMWEBRTC=OFF`) — we only
    ever dial a known dedicated-server address, never P2P/NAT-punched. Also
    restricted `GIT_SUBMODULES` on the FetchContent_Declare to skip the
    multi-hundred-MB `webrtc` submodule (**note**: v1.6.0 renamed the other
    required submodule from `picojson` to `vjson` — check `.gitmodules` again
    if the pin moves).
  - **Crypto backend**: `USE_CRYPTO=BCrypt` on Windows (built into the OS, no
    extra dependency); Linux/macOS use GNS's default, system OpenSSL
    (`libssl-dev` / `brew install openssl`).
  - **Link statically** (`GameNetworkingSockets::static`, `BUILD_SHARED_LIB
    OFF`) — every other dependency here is static/header-only; a shared GNS
    lib would need copying next to the .exe on Windows (no RPATH equivalent).
  - **GNS's public headers need the SYSTEM-include treatment**: same fix as
    doctest/raygui/toml++ (`get_target_property(... INTERFACE_INCLUDE_DIRECTORIES)`
    + re-add `SYSTEM`) — its headers trip `-Wold-style-cast`/`-Wsign-conversion`/
    `-Wlanguage-extension-token` under our `-Werror` set. Done in
    `src/core/CMakeLists.txt`.
  - **macOS CI does NOT build `VB_WITH_NET` yet** — it's a universal
    (arm64+x86_64) build, and a brew-installed protobuf is single-arch, which
    breaks linking the slice that doesn't match the runner's host arch. Needs
    a universal protobuf (vcpkg triplet, or building protobuf from source for
    both arches) before enabling.
  - **`connect()` only accepts numeric IP literals** — `SteamNetworkingIPAddr::
    ParseString()` doesn't resolve DNS. "localhost" / hostnames need
    `getaddrinfo` added to `GnsTransport::connect`; not done.
  - **Local-box-only linker quirk (not a CI concern)**: plain `clang++.exe`
    (GNU driver) targeting the MSVC ABI makes CMake's `MSVC` variable true, so
    GNS's *vendored* abseil submodule (still built even with ICE off — some
    non-ICE code apparently uses it) applies MSVC-style linker flags
    (`-ignore:4221`) that only a Microsoft-syntax-aware driver understands.
    Plain `clang++` rejects it ("unknown argument"); **`clang-cl.exe`** (same
    LLVM install, MSVC-compatible driver) accepts it fine. Real CI is
    unaffected — Windows CI uses actual `cl.exe` via `msvc-dev-cmd`,
    Linux/macOS CI never sets `MSVC` true. Local verification builds on this
    box needing `VB_WITH_NET` should use `clang-cl`/`clang-cl++`, not
    `clang`/`clang++`.
  - Full local validation trail: GNS built standalone under both plain
    `clang++` (compiles; only the unrelated abseil link flag fails) and is
    expected clean under `clang-cl` (build was in flight when this was
    written — check `voxel_browser`/`voxel_browser_server`/`vb_tests` actually
    linked before trusting this note blindly next time).
  - **Smoke-test regression caught and fixed**: `server_smoke`/`client_smoke`
    (default build, `VB_WITH_NET` off) broke the moment the binaries started
    actually calling `listen()`/`connect()` instead of being no-ops. Fixed by
    (a) making the server treat `kBackendUnavailable` specifically as
    non-fatal (see above) and (b) flipping `client_smoke`'s CTest expectation
    — with no `--singleplayer`/`--server` override it now legitimately tries
    to reach `127.0.0.1:27015` with nothing listening, so the smoke test
    became "fails gracefully with a clear message," not "succeeds";
    `PASS_REGULAR_EXPRESSION` alone (not `WILL_FAIL` — the two combine by
    inverting an already-passing regex match into a reported failure, don't
    stack them) asserts that. Lesson: wiring a previously-inert code path for
    real changes what "the binary just starts up" tests actually exercise —
    re-check smoke tests whenever a stub becomes real.

- **2026-09-11 — Entity billboard-sprite placeholder implemented** (Phase 3.5,
  `7f64581`). `inc/vb/render/entity_visual.hpp` (header-only, no raylib,
  unit-tested like `camera.hpp`): `resolve_anim_clip` (priority state machine),
  `bearing_degrees`/`direction_bucket`/`select_pose` (direction math, mirroring
  for the far half of the sectors), `DirectionBucketTracker` (flicker
  hysteresis), `EntityPresentationState` (per-entity orchestration + clip-time
  tracking). `vb/render/entity_renderer.{hpp,cpp}`: raylib-backed, pImpl (no
  raylib types in the header, matching `ChunkRenderer`); one `DrawBillboardPro`
  per tracked entity against a 1×1 white texture tinted by a deterministic
  `NetId` hash (stand-in art, so simultaneous entities are at least visually
  distinguishable). Wired into `main.cpp` next to `chunk_renderer`, same
  headless guard, overlay now shows an `entities N` count.
  `tests/unit/entity_visual_test.cpp` — 14 cases. 113/119 total (non-Lua/Lua
  builds) test cases green.
  - **Gotcha:** `resolve_anim_clip`'s horizontal-speed calc needs both
    operands explicitly cast to `double` before multiplying
    (`static_cast<double>(vel.x) * vel.x` still promotes the second operand
    implicitly) — `-Wdouble-promotion` catches this, same class of trap noted
    for Phase 1.5's camera code.
  - **Confirmed, not assumed:** raylib 5.5's `DrawBillboardPro` with a fixed
    `up = {0,1,0}` gives exactly Y-axis (cylindrical) billboarding for free —
    read `rmodels.c` directly rather than trusting the header comment; its
    `right` vector comes from the camera view matrix but is always horizontal
    regardless of pitch (cross of any forward vector with world-up has zero Y
    component).
  - **Not yet visually verified**: no second connected player exists to look
    at (needs `GnsTransport`, or an in-process 2-client test harness with a
    window) — correctness rests on the unit tests + code review, not eyes on
    screen. Flag if a real two-player session shows different behaviour than
    the math predicts.
  - **Still open:** `SpriteVisual` ECS component (waits on 4.2 having a real
    per-kind visual def to populate it with); the `dead`/`hurt_pulse`/`acting`
    flag bits are defined but nothing sends them yet (server only ever sets
    bit 0).

- **2026-09-11 — Entity visual presentation decided: billboard sprites**
  (committed `adfc68e`).
  (design only, no code). Players/entities are Don't Starve-style 2D
  billboards, not 3D blocky models — `ARCHITECTURE_SPEC.md` §11.3, open
  question §19 Q7, backlog `REMAINING_TASKS.md` Phase 3.5. Closes a real,
  previously-silent gap: nothing has ever drawn a remote player client-side —
  `remote_entities()`/`interpolated_pos()` (Phase 3.4) are correct but nothing
  visible used them. Key technical finding baked into the design: raylib's
  `DrawBillboardPro(..., up={0,1,0}, ...)` gives exactly the wanted Y-axis
  (cylindrical) billboard for free — checked directly against `rmodels.c`
  (5.5): its `right` vector comes from the camera view matrix but is always
  horizontal regardless of pitch (cross of any forward vector with world-up has
  zero Y component), so no custom quad math is needed. Plan: Phase 3 ships a
  hardcoded single-frame placeholder (no Lua/pack dependency, mirrors how
  Phase 2 shipped a mesher ahead of Cellulose); real art + the
  `vb.register_entity{ visual = {...} }` surface land with 4.2/4.4/5.1.

- **2026-09-11 — Bugfixes from the first manual smoke test** (committed `009195e`).
  Three real bugs found by testing on Windows + macOS:
  1. **`sol::nil` doesn't exist on Apple platforms** — sol2 disables its `nil`
     alias by default whenever `__MAC_OS_X_VERSION_MAX_ALLOWED`/`__OBJC__`/a
     `nil` macro is visible (avoids clashing with Objective-C `nil`); see
     `sol/version.hpp` `SOL_NIL_I_`. Fixed by using `sol::lua_nil` (the
     always-defined underlying constant) instead of `sol::nil` in `vm.cpp`.
     **Lesson: never use `sol::nil`/`sol::type` names that shadow platform
     macros — prefer the `lua_`-prefixed sol2 spellings.**
  2. **Sprint had no effect (and walking was already ~17% under `walk_speed`)**
     — `step_movement` applied ground friction *every* tick regardless of
     input, fighting the acceleration step every frame. The fixed point of
     that tug-of-war is `accel/friction` (≈3.75 m/s with the shipped
     constants) **independent of the wish speed**, so both walk (4.5) and
     sprint (7.0) converged to the same capped speed. Fixed: friction now only
     applies when there's no active wish direction (skid-to-a-stop on
     release); acceleration alone drives velocity toward `wish_vel` while
     moving. Regression tests added (`physics_test.cpp`): sustained-movement
     reaches `walk_speed`/`sprint_speed`, and releasing input decelerates via
     friction. **Lesson: an FPS-style accel/friction model needs friction
     gated on "no input", not unconditional — the two tests that existed
     before only checked qualitative behaviour (stops at a wall, settles on a
     floor), never a steady-state speed value, so this shipped unnoticed.**
  3. **Mesh corruption near water ("holes"/wrong AO, worst on beaches)** —
     `chunk_mesher` only culled faces against `is_opaque` neighbours; water is
     non-opaque, so nothing culled water-against-water internal faces. A
     submerged region could emit enough vertices to overflow raylib's
     `Mesh.indices` (`unsigned short*`, 65535 max), silently wrapping the index
     and corrupting the chunk's whole mesh — exactly the beach-adjacent
     glitches reported. Fixed: face culling (and AO sampling) now also treats
     `is_liquid` neighbours as face-blocking, so water meshes as a solid-looking
     box (proper transparency stays Phase 4.3). Added a defensive
     `kMaxMeshVertices` cap as a backstop against any future case that still
     overflows — degrades to a truncated mesh instead of corrupted geometry.
     Regression tests added (`mesher_test.cpp`): a full water chunk only meshes
     its outer shell; water-on-stone culls the shared face both ways.
     **Lesson: `raylib::Mesh` is a hard 16-bit-index format — any per-chunk
     mesh generator needs either a face-count safety margin or an explicit cap;
     there wasn't one.**

  **Follow-up (same session):** the water-culling fix didn't fully address
  "AO is wrong" — it was two separate bugs. The real AO bug: a single shared
  `kCornerUV` 4-entry `{su,sv}` sign table was reused for all 6 mesher faces,
  but each face's `kFaceCorners` winding sits differently relative to its own
  `kFaceTangents` axes; only `+X` happened to match the table by coincidence
  (`-X`/`+Z` had `u` fully inverted, `+Y` was rotated by one corner, `-Y` was
  mirrored) — AO landed on a *different* corner than the one actually
  occluded, on 5 of 6 face orientations. Fixed by computing each corner's
  `(u,v)` sign directly from its own coordinates dotted with that face's
  tangent axes (`corner_sign()`) instead of a hand-matched table — correct by
  construction, no per-face bookkeeping to get wrong. Verified the regression
  test actually catches it: reverting just the fix reproduces the bug (bright
  vertex where the occluder sits, a different vertex wrongly darkened).
  **Lesson: a shared lookup table indexed by "corner number" across 6 faces
  with independently-chosen winding + tangent-axis conventions is exactly the
  kind of thing that looks right for the first case you check (+X) and is
  wrong for the rest — derive from geometry instead of hand-deriving a table
  per case.**

  **Known, deliberately not fixed:** auto-step-up "jerks" the camera (the
  physics teleports the feet up ~1 block in a single tick — correct and
  robust, but visually abrupt). Fixing it well means smoothing the *rendered*
  eye height independently of the physics position (physics/prediction must
  stay exact; only the camera can lag), which is real work I didn't want to
  rush alongside the two correctness bugs above. Tracked in
  `REMAINING_TASKS.md` Phase 3.3 as a follow-up.

- **2026-09-11 — Phase 5.2 block breaking / placing over the network**
  (committed `e5e5640`). `C2S_BlockEdit` / `S2C_BlockEditResult` (`vb/protocol/world`) →
  **`kEngineProtocolVersion` 2 → 3**. `WorldReplicator::apply_block_edit` (reach
  ≤5.5, target validity, no-floating-placement, whole-chunk `relight_chunk`,
  builds an `S2C_ChunkDelta` with the block + diffed light bytes, fans out to
  every player whose `last_sent_` has that chunk). `ServerSession::handle_block_edit`
  routes it. `ClientSession::push_block_edit` (optimistic apply to `chunks_`,
  rollback on a `!accepted` result, drop pending on the authoritative delta) +
  `ClientChunkStore::edit_block` (bumps the chunk + bordering chunk revisions so
  the renderer re-meshes). Client `main.cpp`: Amanatides–Woo voxel raycast from
  the eye, wire-cube highlight, LMB break / RMB place stone (singleplayer path).
  Tests: `tests/unit/blockedit_test.cpp` (round-trip + 2-client fan-out + reject
  rollback).
  - **Lua veto seam:** `apply_block_edit` has a `[Phase 4.2]` comment where the
    `block_break`/`block_place` handler hooks in; region protection too.
  - **Not done:** cross-chunk light propagation on edit (relight is per-chunk),
    break progress / tool times (instant break), drops (needs items — Phase 5.1),
    `S2C_BlockEditResult` reason codes.

- **2026-09-11 — Phase 4.1 Lua VM** (committed `2e3d297`). `inc/vb/script/vm.hpp` +
  `src/script/vm.cpp`: `vb::script::Vm` — pImpl over one `sol::state`, sandboxed
  at construction (base/string/table/math/coroutine/utf8 only; os/io/load/require/
  package/collectgarbage nilled; `debug` trimmed to `traceback`), ceiling
  allocator, per-call `lua_sethook(LUA_MASKCOUNT)` instruction budget. `do_string`
  is source-only. `ScriptError` enum added to `core/error.hpp`. `tests/unit/
  script_test.cpp` (7 cases, `#if VB_WITH_LUA`). CI build workflows now pass
  `-DVB_WITH_LUA=ON` (Lua is pure C, no system deps; sol2 header-only).
  - **sol2 pin bumped v3.3.0 → v3.5.0**: 3.3.0's bundled "better optional"
    (`optional_implementation.hpp`) fails to compile under Clang ≥ 18
    ("no member named 'construct' in optional<T&>"). `docs/lua-api.md` +
    `Dependencies.cmake` note it.
  - **vm.cpp compiles in every build**: `#if !VB_WITH_LUA` gives a `kDisabled`
    stub, so `vb_core` always has the symbols and `script_test.cpp` links.
  - **Local dev:** `cmake -S . -B build-lua -DVB_WITH_LUA=ON -DVB_WITH_COMPRESSION=ON`
    — first configure re-fetches lua + sol2 (~45s). `build/` stays Lua-off.
  - **Not thread-safe:** the instruction hook assumes one `Vm` per thread (fine —
    the pack VM lives on the server tick thread).
  - **Next (4.2):** `vb.register_*` + event bus + `vb.world` API on top of `Vm`;
    (4.3) `S2C_BlockRegistry`; (4.4) asset sync + virtual FS `require`.

- **2026-09-11 — Phase 3 physics + netcode slice** (committed `37e39a3`). New
  `vb/physics/movement.{hpp,cpp}` (`step_movement` — shared server + client
  prediction; substepped swept-AABB w/ bisection snap, gravity/friction/jump/
  step-up/fly; `MoveParams`). `vb/protocol/input.{hpp,cpp}` (`InputCmd` /
  `C2SInputBatch`, lane 4). `S2C_EntitySnapshot` gained `has_local` + `local`
  record + `flags` bit0=on_ground → **`kEngineProtocolVersion` 1 → 2**.
  `ServerSession`: `handle_input_batch` (authoritative movement, dt clamp,
  seq dedup), per-`Conn` `MoveState`; snapshots now carry the recipient's own
  state. `ClientSession`: `push_input` (predict + history ring + resend),
  reconcile-on-snapshot, `remote_samples_` + `interpolated_pos`. Client
  `main.cpp` walks input-driven (collides with terrain) instead of free-fly.
  `inc/vb/ecs/components.hpp` (struct defs only). Tests: `physics_test.cpp`,
  `netcode_test.cpp`. ~99.5k assertions, green under `-Werror`.
  - **Gotcha:** step-up in a pure-voxel world needs `step_height > 1.0` to climb a
    full block (default is `1.05`); Quake-style 0.55 climbs nothing here.
  - **Gotcha:** `ServerSession` intercepts *all* post-join C2S frames now (input
    batch handled, others ignored) — it no longer forwards them to the handshake
    FSM. Fine today; revisit when C2S block-edit/chat land.
  - **Deferred:** EnTT registry + system runner (3.1), wall-clock server-time
    estimation + librg entity mapping (need real `GnsTransport`).

- **2026-09-11 — Phase 2 complete** (`ce7ee63`..HEAD). `vb/world`:
  `PalettedChunkStore`, `Chunk`, `World`, `BlockRegistry`, `LightEngine`
  (per-chunk flood fill), `chunk_codec` (RLE), `chunk_interest`,
  `ChunkLifecycleSystem`, `ClientChunkStore`, `chunk_mesher` (face-cull + AO).
  `vb/worldgen`: deterministic `vb/core/noise.hpp` (no trig, `-ffp-contract=off`
  project-wide), `WorldGenerator` (fBm heightmap), `WorldGenWorkerPool`
  (+`kSynchronous`). `vb/protocol`: `S2C_EntitySnapshot`, `S2C_Chunk*`.
  `net/world_replicator`: per-tick chunk streaming, wired into `ServerSession`
  (`set_world_replicator`) + `ClientSession`. `vb/render/chunk_renderer`: raylib
  GPU upload. `voxel_browser --singleplayer` = worldgen + streaming + meshing +
  rendering, one code path. Determinism golden `0x021BB3847413D8A5` green on
  all 3 platforms. ~99k test assertions.
- **GCC gotcha:** `uint64_t` (`unsigned long` on LP64) vs `...ULL` literals
  (`unsigned long long`) trips `-Wsign-conversion` — always name wide constants
  `constexpr std::uint64_t`.
- **Reference-lifetime gotcha:** don't store `const BlockRegistry&` — callers
  pass `BlockRegistry::base()` temporaries. `LightEngine` holds it by value.

- **2026-09-10 — Phase 1.4 replication** (uncommitted). librg spike done →
  `docs/replication.md` + `ARCHITECTURE_SPEC.md §19 Q3` (librg v7.4.0 is a
  self-contained header, zpl bundled; use it for culling + create/update/remove
  framing, our codec for payloads; not wired yet). `Dependencies.cmake` librg
  block fixed (`v7.4.0`, `vb_librg` INTERFACE target, no separate zpl).
  `inc/vb/replication/interest.hpp` (`InterestGrid` + `diff_interest`, header-
  only, linear scan). `vb/protocol/snapshot.{hpp,cpp}` (`S2CEntitySnapshot`).
  `ServerSession` broadcasts per-player snapshots each tick;
  `ClientSession::remote_entities()`; `ServerSession::set_player_state()`.
  `tests/unit/replication_test.cpp` — two-client visibility (Phase 1 exit
  criterion). 46 cases / 280 assertions.
- **CI gotcha (found on first push):** `doctest.h` trips MSVC `/W4 /WX`
  (`C2220` at doctest.h:539). Fix: `tests/CMakeLists.txt` re-adds
  `doctest::doctest`'s include dir as `SYSTEM PRIVATE`. clang/gcc never hit it,
  so it must be verified in CI. Same pattern already used for raygui/toml++.

- **2026-09-10 — Phase 1.1 TOML config** (uncommitted). `tomlplusplus` v3.4.0
  added to `Dependencies.cmake` (always-on, header-only, linked PRIVATE into
  `vb_core`). `vb/core/config.{hpp,cpp}`: `ServerConfig`/`ClientConfig` (spec
  §15 defaults), `parse_*`/`load_*` (missing file → defaults, malformed →
  `kParseError`), `apply_cli_overrides`. Both binaries load `--config` +
  overrides. `server.toml.example` / `client.toml.example` in repo root;
  `/server.toml` `/client.toml` gitignored. `tests/unit/config_test.cpp`.
  41 test cases / 242 assertions green.

- **2026-09-10 — Phase 1.5 client shell** (uncommitted). `vb/render/camera.hpp`
  — header-only `FirstPersonController` (double precision throughout; the strict
  `-Wdouble-promotion` warning makes mixed float/double painful, so everything
  angle/position is `double` and `LookMoveInput` holds `Vec2d`/`Vec3d`). Client
  `main.cpp` gained mouse-look + WASD + a debug overlay + spawn-from-JoinAccept.
  `tests/unit/render_test.cpp` (5 cases). Rule: no `1.0f` literals anywhere the
  value flows into a `double` — CI's `-Wdouble-promotion` is fatal.

- **2026-09-16 — Phase 4 complete (4.1–4.5), uncommitted: sandboxed server
  Lua pack API, block registry replication, asset sync protocol, and a
  second client-side Lua UI VM, all wired end-to-end over
  `LoopbackTransport` and verified in both `VB_WITH_LUA=ON`/`OFF` builds.**
  `kEngineProtocolVersion` went 3→4 (4.2, `S2C_Chat`) →5 (4.3,
  `S2C_BlockRegistry`) →6 (4.4, asset-sync messages) →7 (4.5, `C2S_UiEvent`).
  Mechanism only — **no `content/base` pack exists (Phase 5.1)**, so nothing
  calls `vb.register_block`/`ui.define`/etc. at real runtime today outside
  of tests; `docs/lua-api.md`/`docs/protocol.md`/`REMAINING_TASKS.md` all say
  this explicitly, don't let it read as "Phase 4 not done."
  **Gotchas/landmines for whoever touches this next:**
  - **xxHash/lz4 header collision, already fixed — don't undo the link
    order.** lz4's vendored source ships its own old `lib/xxhash.h` with no
    `XXH3_128bits`/`XXH128_hash_t`. `src/core/CMakeLists.txt`'s
    `VB_WITH_COMPRESSION` block links `xxHash::xxhash` **before**
    `LZ4::lz4` on purpose (comment left in place) so the real header wins
    `-I` search order. Relinking `LZ4::lz4` first silently breaks
    `assetsync/manifest.cpp`'s hashing with cryptic "unknown type name"
    errors from inside a *different* header than the one you'd suspect.
  - `std::vector<std::byte>` can't be built directly from
    `std::istreambuf_iterator<char>` (no implicit `char`→`std::byte`
    conversion) — `assetsync/cache.cpp` has a `read_whole_file()` helper
    (`ifstream` + `tellg`/`seekg`/`.read()` + `reinterpret_cast<char*>`) for
    this; reuse it rather than re-deriving the istreambuf_iterator pattern.
  - `vb::net::ClientSession::send_ui_event`/similar helpers inside
    `session.hpp` are already in `namespace vb::net` — call `send_message`
    unqualified there, not `net::send_message` (that resolves to a
    nonexistent `vb::net::net`).
  - **Known, documented gap, not a bug:** `vb.world.set_block()` (the pack
    API's direct world-mutation call) does **not** run the relight cascade
    that `C2S_BlockEdit` triggers — can desync lighting until something
    else touches the chunk. No test covers a pack calling this yet because
    no pack does. Fix when a real pack needs `vb.world.set_block` for
    something other than worldgen-time setup.
  - **UI layout is evaluated once at `open()`, never re-run.** A
    server-driven UI that wants to show different content mid-session must
    `ui.close()` then have the server call `player:open_ui()` again — there
    is no re-layout/refresh call. Documented in `docs/lua-api.md`, not
    fixed — if this becomes a real pain in 5.x, the fix point is
    `UiRuntime::Impl::open()` (`src/script/ui_runtime.cpp`).
  - **Widget set is missing "item grid"** (the one spec-named widget type
    not implemented) — deferred because it needs a real item system
    (5.1). Everything else (label/panel/button/textbox/list) is done.
  - **Asset-sync reconnect fast-path is in-session-only by explicit user
    choice** (not persisted across process restarts) — the on-disk
    content-addressed cache (`ClientAssetCache`, `<cache_dir>/<2-hex>/<32-hex>`)
    does still make repeat-connect data transfer zero-byte even across
    restarts (verified by test: "second connection transfers nothing"), but
    the manifest round-trip (listing what's needed) always happens fresh
    each connect — no skip-if-already-synced shortcut before that. If a
    future phase wants to skip the manifest listing too, that's new scope,
    not a bug.
  - `PackRuntime`'s Lua-visible player object is one merged usertype
    (player + would-be-entity) since no EnTT registry exists yet (Phase
    3.1 still open) — `vb.register_entity`'s `on_spawn`/`on_tick`/etc.
    callbacks are captured but **nothing ever calls them**.
    `vb.worldgen.set_pipeline` is bound but errors as a nil call — not
    implemented at all, out of scope for 4.x.
  - Test-design trick worth remembering: there's no event that hands a Lua
    script a `PlayerHandle` right at `player_join` time with a live net_id
    (join fires from inside `authenticate`, before a session exists) — both
    `pack_runtime_integration_test.cpp` and the UI round-trip test trigger
    `player:open_ui()`/inventory-style flows from inside a `block_break`
    handler instead, which does have a real handle. Reuse this pattern for
    any future test that needs a Lua-side player object outside of tick.
  - Full verification each phase: `ctest` green in `build-lua`
    (`VB_WITH_LUA=ON` + `VB_WITH_COMPRESSION=ON`, all cases) and
    `build-release` (`VB_WITH_LUA=OFF`/`VB_WITH_COMPRESSION=OFF`, confirms
    every disabled-stub class compiles/links and both binaries still run).
    The one recurring failure across both configs is the same
    pre-existing `gns_transport_test.cpp` real-UDP-socket sandbox
    limitation documented elsewhere in this file — not a regression from
    this work.
  - **Not done, deliberately out of scope for 4.x:** no real content pack
    (5.1); `require`-over-synced-virtual-pack-FS (the client's
    `virtual_pack_fs()` exists but nothing consumes it — no module loader
    wired to the synced asset cache yet); per-callback wall-clock budget
    enforcement (only instruction-count budget exists); a visually-verified
    live UI render (no GL context available in this environment — rests on
    `ui_runtime_test.cpp`/`pack_runtime_integration_test.cpp` + code
    review, same caveat as the rendering-related entries above).

