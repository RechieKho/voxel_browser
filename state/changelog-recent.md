# Changelog (detail) — Recent entries not yet folded into §8's log

> Full landed-feature writeups for phases that were only ever recorded in STATE.md's
> old cascading header narrative, never copied into the structured §8 log (whose newest
> entry there is Phase 6.11) — so these are NOT duplicated in `changelog-part1.md`/
> `changelog-part2.md`. Covers, newest first: the 6.18 block-self-heal follow-up, Phase
> 6.18 (discrete punch combat), Phase 6.17 (decoupled block breaking + MovementBindings),
> Phase 1.3 networking polish (two-process smoke test, per-IP connection cap, hostname
> resolution), Phase 6.15 (kitchen-sink example pack), Phase 6.14 (Lua-driven worldgen/
> FastNoise2), Phase 6.13 (read-only server config visibility), and back through the
> already-logged 6.11/6.10/6.9/6.8/6.7/6.6/6.5/6.4/6.1/5.5/5.1-5.4 summaries (those repeat
> §8 content in brief — see `changelog-part1.md`/`changelog-part2.md` for their full
> writeups).

---

Last updated: 2026-09-18 (Phase 6.18 follow-up -- block self-heal: a block
that stops taking punches now heals back to full over time instead of
keeping an accumulated punch count forever. `ServerSession::PunchParams`
gained `heal_after_seconds` (default 4.0, idle time since the last landed
punch before healing starts) and `heal_interval_seconds` (default 1.5,
-1 punch every this many seconds once eligible) -- both overridable via
`vb.combat.set_params{heal_after_seconds=, heal_interval_seconds=}`
alongside the fields already there. Unlike 6.5's `BlockDamageSystem` (zero
built-in heal policy until a pack supplies one -- there's no begin/stop
lifecycle for punching to hang a policy off of the same way), this is a
real engine default; only the *rate* is pack-facing, not whether healing
happens at all. `block_punch_counts_`'s value type is now `PunchDamageState
{punches, idle_seconds, heal_progress}` instead of a bare count; a new
`ServerSession::update_block_punch_healing(dt)`, called from `tick()`
right after the unrelated `update_block_damage()`, decrements idle entries
and erases ones that fully heal. Landing a punch resets both timers on
that position -- a fresh hit undoes partial heal progress rather than
adding to it. Negative `heal_after_seconds` disables healing outright.
Three new `blockedit_test.cpp` cases (idle-heals-to-full-then-fresh-punch-
starts-at-1; a punch resets the idle clock instead of heal continuing from
before it; the pre-existing N-punches-to-break case unaffected since it
never idles). Full `vb_tests` green (289/289, up from 287); all 4 CTest
cases pass.
Previous entry: 2026-09-18 (Phase 6.18 -- Growtopia-style combat: attack is now
a discrete "punch" per click, not Minecraft-style holding; see
`REMAINING_TASKS.md` 6.18 for the full writeup, this is a summary). New
public `ServerSession::punch(NetId)` (`inc/vb/net/session.hpp`+`src/net/
session.cpp`) raycasts blocks and nearby players (a vertical cylinder --
feet to `MoveParams::height`, radius `PunchParams::hit_radius` -- not a
single point, so aiming anywhere along a standing player's body registers)
along the puncher's authoritative yaw/pitch and resolves to whichever is
closer: a player hit calls the existing `damage_player()`; a block hit
increments a new `block_punch_counts_` map and breaks it via 6.17's
`apply_script_block_edit()` once `BlockType::max_damage` punches land (0
still means "first punch breaks it"). `vb.combat.set_params{reach=,
hit_radius=, player_damage=}` overrides the defaults, mirroring
`vb.physics.set_params` exactly, wired into both `src/server/main.cpp` and
`--singleplayer`. `player:punch()` is the Lua-facing wrapper (returns a
`{hit_player, target, hit_block, x,y,z, punches, broken}` table).
`content/base/mechanics.lua` was rewritten to edge-detect `buttons.primary`
(same `was_down` idiom `kitchen_sink/keybinds.lua` uses) and call
`player:punch()` once per rising edge -- no hold-timer concept left in it at
all, unlike 6.17's version of this same file. New shared `core::
forward_from_yaw_pitch()` (`inc/vb/core/math.hpp`) replaces the trig that
used to live only inside `FirstPersonController::forward()`, now also used
server-side (no camera object exists there) -- `camera.hpp` delegates to it.
Movement is explicitly untouched (user constraint going in: don't move
movement to a raw-key-event model, keep client-side prediction intact) --
6.17's `MovementBindings`/continuous `InputCmd.move` pipeline is unchanged.
New tests in `tests/unit/blockedit_test.cpp` (plain `ServerSession`, no
Lua) cover: instant break at `max_damage == 0`; exactly N punches to break
a custom `max_damage = N` block (built via `BlockRegistry::add_or_get`, not
Lua); player-over-block precedence when both are candidates; a clean no-hit
result when nothing is in reach. Full `vb_tests` green (287/287, up from
283); all 4 CTest cases pass. **Known simplification, not attempted:** no
engine-side punch-rate cooldown -- a pack that doesn't edge-detect (or a
macro) could call `punch()` every tick; left as the calling pack's own
responsibility, same posture as every other "engine provides the primitive"
seam in this codebase.
Previous entry: 2026-09-18 (Phase 6.17, shipped with a different shape than
`REMAINING_TASKS.md` originally planned — see that section's own note for the
full writeup). Summary: block breaking is no longer an engine default at all.
`src/client/main.cpp`'s hardcoded 5.2-era hold-to-break timer (`breaking`/
`break_target`/`break_progress`/`kBreakSeconds`) is deleted outright, not
replaced by another client-side timer -- the client now only reports raw
`buttons.primary`/`.secondary` (the wire protocol's pre-existing, previously-
unused `kInputPrimary`/`kInputSecondary` bits) through the same
`vb.on("player_input", ...)` channel Phase 6.3 already built. New engine
primitive: `player:break_block(x,y,z)` (`PlayerHandle`, `src/script/
pack_runtime.cpp`) backed by a new public `ServerSession::
apply_script_block_edit(editor, action, pos, block)` (`inc/vb/net/
session.hpp`+`src/net/session.cpp`) that runs the *exact* `C2S_BlockEdit`
pipeline (reach check, `block_break`/`on_break` hooks, drops, relight,
fan-out) without a wire frame -- extracted verbatim from `handle_block_edit`'s
body, now shared by both the real network path and this script-triggered one.
`content/base/mechanics.lua` (new file, auto-loaded by `pack_loader.cpp`'s
generic "any other root-level `.lua`" pass, no engine change needed) is
content/base's own hold-to-break: a `player_input` handler keyed by
`player:get_name()` (matches `kitchen_sink/keybinds.lua`'s existing
convention), does its own `vb.world.raycast` from yaw/pitch, and calls
`break_block()` after 0.35s -- a pack that never loads this file gets zero
breaking behavior from holding LMB, proven by a new `pack_runtime_
integration_test.cpp` case. `build_input_table`'s Lua-facing input table
gained a `dt` field (additive, backward compatible) since a pack has no other
way to measure real elapsed time per `player_input` call for its own hold
timer. Movement's WASD/jump/sprint keys were pulled out of
`sample_input_cmd`'s inline branching into a `MovementBindings` struct
(same file) with identical default keys -- purely a client-local
"one table instead of scattered literals" refactor, since movement was
already fully pack-overridable server-side via `vb.on("player_input", ...)`
returning a replacement table (pre-existing `ServerSession::
handle_input_batch` behavior, unchanged); movement was deliberately kept off
`vb.register_keybind` entirely, sidestepping 6.17's original "does a
continuous axis fit the boolean keybind registry" open question rather than
answering it.

**Known regression, deliberately accepted, not yet fixed:** `client.
break_progress()` (6.16's HUD hook) now always returns `nil` -- the local
timer it used to read is gone, and it was never wired to anything
server-authoritative (that needs the still-unimplemented "replicate
`BlockDamageSystem`'s damage *value*, not just begin/stop/complete" half of
Phase 6.5 -- `BlockDamageTickResult::changed`/`cleared` are computed and
thrown away today, same gap 6.5's own original entry already flagged).
`content/base/ui/hud.lua` silently draws no progress bar until that lands --
this is a real, visible UX regression from before this session, traded
deliberately for "breaking is opt-in content" per explicit user direction.
`BlockDamageSystem`/`C2S_BlockBreakBegin`/`Stop` (6.5) remain completely
unused by `content/base` (every shipped block still has the implicit
`max_damage = 0`); `mechanics.lua`'s hold timer is a pack-side Lua clock, not
a `BlockDamageSystem` consumer -- same one-flat-duration-for-every-block
posture the old hardcoded C++ timer had, just relocated. Full `vb_tests`
green (283/283, up from 282) on `build-net-lua`; all 4 CTest cases pass.
Previous entry: 2026-09-18 (Phase 1.3 networking polish, post-Phase-6: three
items from §1.3's leftover bullet list. (1) **Manual two-process smoke
test** — `voxel_browser_server.exe --port 27099` in one background process,
`voxel_browser.exe --headless --frames 5 --server 127.0.0.1 --port 27099`
in a separate one, real UDP over loopback (`VB_WITH_NET`, `build-net-lua`):
a real join completes end to end. No code gap, purely a verification step.
(2) **Per-IP connection cap**: `Transport` (`inc/vb/net/transport.hpp`)
gained `remote_address(ConnId) -> optional<string>` (default `nullopt`;
`GnsTransport` overrides it via `GetConnectionInfo`, `LoopbackTransport`
keeps the default since there's no real network identity in-process) +
`ServerSession::set_max_connections_per_ip(int)` (`0` = unlimited),
enforced in `tick()`'s `kConnected` handling by counting already-tracked
`Conn::remote_address` matches and `Transport::close()`-ing a new
connection before any handshake traffic if the cap is already met — a
deliberately *transport-level* rejection, not an application-level one like
`max_players` (`handshake.cpp`), since IP identity doesn't exist at the
transport-agnostic handshake-FSM layer at all (it's tested over
`LoopbackTransport` too, which has no IPs). New `ServerConfig::
max_connections_per_ip`/`server.toml` key, wired in `src/server/main.cpp`;
also exposed via `vb.config.get("max_connections_per_ip")` (6.13's
surface). (3) **Hostname resolution**: `GnsTransport::connect()`'s
`SteamNetworkingIPAddr::ParseString()` only ever accepted numeric IP
literals -- added a `getaddrinfo()`/`freeaddrinfo()` fallback
(`resolve_hostname()`, `src/net/gns_transport.cpp`) whenever `ParseString`
rejects the input, so `"localhost"`/a real hostname now resolves (prefers
IPv4, falls back to IPv6). Windows needs `<winsock2.h>`/`<ws2tcpip.h>` +ve
a scoped `WSAStartup`/`WSACleanup` pair around the resolve call (not
relying on GNS's own internal Winsock init, which is undocumented
behavior); POSIX uses `<netdb.h>` — **the POSIX branch is unverified this
session** (Windows-only dev machine, see `STATE.md.local`; the code path
is the standard portable `getaddrinfo` pattern, just not build/run-tested
on Linux/macOS here). New tests: `gns_transport_test.cpp` gained
`remote_address()`-over-real-UDP, a full `ServerSession` per-IP-cap
accept/reject case, and a `connect("localhost", ...)` case (plus a
genuinely-unresolvable-hostname-still-fails-cleanly check);
`config_test.cpp`/`pack_runtime_test.cpp` cover the new config field.
Two items from the same §1.3 list deliberately **not** attempted:
`ENGINE_PROTOCOL_VERSION` mismatch surfaced in the client connect UI (touches
Phase 5.3's main menu, out of scope for this pass) and macOS CI's universal-
protobuf gap (`build_macos.yml`) -- can't be verified at all without a Mac,
too risky to edit blind. Full `vb_tests` green (272/272, up from 269) and
all 4 CTest cases pass on `build-net-lua`.
Previous entry: Phase 6.15 — kitchen-sink example pack: Phase 6 is
now fully done. `content/examples/kitchen_sink/` (new pack directory,
sibling to `content/base/`, never loaded by default) exercises every
Phase 6 "default + override" API at least once with a real, running effect
— `blocks/unstable_ore.lua` (max_damage/max_stack/pickup_radius/
item_lifetime_seconds together on one block), `entities/sentry.lua`
(register_entity, honestly left inert — same pre-existing limitation
`content/base/entities/dropped_item.lua` already documents, no EnTT
dispatch yet), `biomes/savanna.lua`/`tundra.lua` + `worldgen.lua`
(register_biome with real probability/adjacency feeding a real
vb.worldgen.set_pipeline + vb.noise.* graph, a carver, a vein — the worked
pipeline example 6.14's own `content/base` biome files pointed to and never
delivered themselves), `physics.lua`, `daynight.lua`, `mechanics.lua`
(vb.db/vb.crypto.hash + a block_break_tick handler for unstable_ore's
max_damage=6), `death.lua` (player_death with drop_inventory), `chat.lua`
(text-rewriting, contrast with `crafting.lua`'s veto-only use of the same
event), and `keybinds.lua` + `ui/status.lua` (register_keybind +
player_input opening a ui.define screen via player:open_ui). Regression-
tested by new `tests/unit/kitchen_sink_pack_test.cpp` (mirrors
`content_pack_test.cpp`'s "load the real files" pattern for `content/base`)
— confirms the block's fields, `effective_move_params`/
`effective_day_night_curve`/`effective_day_length_seconds`, and
`build_worldgen_pipeline`'s biome/carver/vein counts all actually took
effect, plus a real block-output-differs-from-default check reusing 6.14's
own pattern. Also manually ran `voxel_browser_server.exe --content-pack
content/examples/kitchen_sink --ticks 5` this session to confirm it starts
cleanly outside the test harness, not just inside doctest. Full `vb_tests`
green (268/268, up from 266) in **both** `build-net-lua`
(`VB_WITH_WORLDGEN=OFF`) and `build-worldgen` (`VB_WITH_WORLDGEN=ON`, real
FastNoise2 — this is the first real, hand-authored Lua pack ever run
through that backend, not just synthetic test tables); all 4 CTest cases
pass in `build-net-lua`. `content/base` itself is completely untouched by
this item.
Previous entry: Phase 6.14 — Lua-driven worldgen pipeline,
FastNoise2 backend: the last unstarted Phase 6 item, and by far the
biggest — four new headers/sources under `vb/worldgen/`
(`noise_graph.hpp/.cpp`, `biome_selector.hpp/.cpp`, `pipeline.hpp`,
`fastnoise2_compile.hpp/.cpp`), plus `PackRuntime::build_worldgen_pipeline`
(`src/script/pack_runtime.cpp`) and `WorldGenerator`'s new optional
`std::shared_ptr<const PackWorldGenPipeline>` constructor param
(`inc/vb/worldgen/generator.hpp`/`.cpp`). Full design writeup (this
session's exploration + the resulting plan) lives in this session's own
transcript; the durable facts:

**API shape deliberately deviates from `REMAINING_TASKS.md`'s original
`vb.worldgen.set_pipeline(fn)` wording** — Lua/sol2 is strictly
single-threaded and `WorldGenWorkerPool` calls `WorldGenerator::generate()`
from N worker threads with zero locking (confirmed: no prior code anywhere
touches a `sol::state` off the main thread), so a literal per-chunk Lua
callback was never viable. Shipped as `set_pipeline(table)` instead: a raw
`sol::table` captured at call time (frozen-guard, same as every other
registration function; `height` is validated non-null eagerly, same
"validate at the call site" posture 6.8's `set_curve` uses for its own
required `keyframes` field), compiled exactly once — main thread, right
after `freeze()`, before constructing `WorldGenerator`/`WorldGenWorkerPool`
— into an immutable `worldgen::PackWorldGenPipeline`. No call at all (the
overwhelming common case) leaves `WorldGenerator` on its byte-identical
pre-6.14 fixed path; `tests/unit/worldgen_test.cpp`'s original golden-hash
test is completely untouched and still passes unmodified.

**`vb.noise.*`** builds a small portable node-graph IR
(`worldgen::NoiseNode` — `constant`/`value`/`cellular`/`fbm`/`remap`/
`combine`) as plain tagged Lua tables (not opaque handles) — the real work
is `PackRuntime::Impl::parse_noise_node`'s recursive parse into pure C++,
done once, not per-voxel. Two evaluators exist for the *same* IR: the
always-available one (`src/worldgen/noise_graph.cpp`, `NoiseNode::eval2/
eval3`) built on `vb/core/noise.hpp` extended this session with `hash3`/
`value3`/`cellular2` (F1 jittered-grid Voronoi noise); and, when
`VB_WITH_WORLDGEN` links real FastNoise2, a compiler
(`src/worldgen/fastnoise2_compile.cpp`, `#if VB_WITH_WORLDGEN`-gated) that
translates the same tree into an actual FastNoise2 `SmartNode` graph
(`Value`/`CellularValue`/`FractalFBm`/`Remap`/`DomainScale`/`Add`/
`Multiply`/`Min`/`Max`, `New<T>()`-constructed). The two backends are **not**
expected to produce bit-identical output (different noise algorithms
entirely) — both just need to be deterministic and reasonably shaped for
the same graph description; `PackRuntime`'s own `build_worldgen_pipeline`
picks the backend via `#if VB_WITH_WORLDGEN` at the two call sites
(`height_field`, each carver's `density`).

**FastNoise2 was actually fetched, linked, and exercised this session** —
attempted per explicit user choice over the safer hand-rolled-only
fallback. Found and fixed a real pre-existing bug while doing it:
`cmake/Dependencies.cmake` pinned FastNoise2 to tag `v0.10.0`, which does
not exist in `Auburn/FastNoise2` (confirmed via `git ls-remote --tags`) —
the real tag is `v0.10.0-alpha`. Went unnoticed since Phase 0 because
nothing had ever actually triggered the fetch before this item (`VB_WITH_
WORLDGEN` was OFF everywhere, zero FastNoise2 call sites anywhere in the
codebase until now). Fixed to `v0.10.0-alpha`. A **new build dir,
`build-worldgen/`**, was configured this session specifically to verify
this (`-DVB_WITH_NET=OFF -DVB_WITH_LUA=ON -DVB_WITH_WORLDGEN=ON` — `VB_
WITH_NET=OFF` only because a *fresh* GameNetworkingSockets fetch hit a
missing system Protobuf that `build-net-lua`'s already-built cache happens
to route around; unrelated to worldgen, not investigated further since net
+ worldgen together was never necessary to prove this item works). See
`STATE.md.local` for the full FastNoise2-fetch/build note. `vb_tests`
(266/266) and all 4 CTest cases pass in **both** configs
(`build-net-lua`, `VB_WITH_WORLDGEN=OFF`, hand-rolled backend; and
`build-worldgen`, `VB_WITH_WORLDGEN=ON`, real FastNoise2 backend) —
confirmed by actually building and running both this session, not assumed.

**Biome selection** (`worldgen::BiomeSelector`,
`inc/vb/worldgen/biome_selector.hpp`/`.cpp`) implements the Voronoi-cell,
adjacency-weighted design finalized 2026-09-17 (quoted in this file's own
history below): a jittered grid (`core::noise::cellular2`) partitions the
world; `resolve_cell` recurses only into neighbor cells whose
`hash(seed, neighbor)` sorts before the current cell's own key (bounded,
per the design note), then draws a biome weighted by
`probability * product(adjacency multiplier vs. each resolved neighbor)`,
floor-clamped (`kAdjacencyFloor = 1e-3`) so no candidate ever hits exactly
0 (spec: soft multipliers, never hard exclusions). **Deliberately not
globally memoized or locked** — recomputes its recursion from scratch on
every `resolve()` call using a purely local, stack-only memo, trading
cache-hit-rate across repeated nearby queries for zero shared mutable
state/locking across `WorldGenWorkerPool` worker threads. Flagged in
`REMAINING_TASKS.md`'s Deferred section as a future perf follow-up if it
ever actually matters (not observed to, this session).

**Carvers/veins/decoration** run as sequential passes inside
`WorldGenerator::generate()`'s new pipeline branch
(`src/worldgen/generator.cpp`) after the per-column height/biome pass, all
seeded from a deterministic per-chunk hash (`core::noise::hash3(seed,
coord)`) via a small counter-based `DetRng` (dependency-free, same
determinism posture as `vb/core/noise.hpp` itself — no `<random>`, whose
engines aren't guaranteed bit-identical in spirit even where the standard
pins their formulas). **Decoration is schematic-only** (a fixed
block-offset list scattered per chunk, `worldgen::DecorationEntry`), not
the spec's "procedural callbacks" — same single-threaded-Lua constraint as
`set_pipeline` itself; a callback per decoration site can't run on a
worker thread either. Offsets landing outside the originating chunk are
silently skipped — **no cross-chunk decoration** (spec's stage 6 wants
trees/structures able to straddle chunk borders; not attempted). Both
narrowings are recorded in `REMAINING_TASKS.md`'s Deferred section,
folded into the existing "rule-based decorative structure placement" entry
that was already waiting on this pipeline landing.

**New determinism test**
(`tests/unit/worldgen_test.cpp`, "worldgen determinism gate, pack-driven
pipeline (golden value)") builds a `PackWorldGenPipeline` directly in C++
(constructing `NoiseNode`/`BiomeEntry`/`CarverDef`/`VeinDef` by hand, not
through Lua) so it stays pinned to the hand-rolled evaluator regardless of
which backend a given build links — confirmed identical golden digest
(`0x33CA94E677AF7922`) under both `VB_WITH_WORLDGEN` on and off this
session. A second, Lua-driven test in `tests/unit/pack_runtime_test.cpp`
("vb.worldgen.set_pipeline + vb.register_biome produce a working
pack-driven pipeline...") goes through the real `PackRuntime` parse path
and therefore *does* exercise whichever backend is linked (no literal
golden hash there — just "differs from the fixed default, reproducible
across two builds of the same pipeline" checks, since the FastNoise2
backend's exact numeric output was never meant to match the hand-rolled
one bit-for-bit).

`content/base`'s existing `biomes/plains.lua`/`forest.lua` are unchanged —
`content/base` itself deliberately never calls `vb.worldgen.set_pipeline`
(stays on the base package's spec §5.1 "minimal/production-shaped" posture;
a worked pipeline example is explicitly 6.15's job, a separate demo pack).
`decoration = "trees"` (a plain string, `forest.lua`'s pre-6.14 usage)
stays captured-but-inert by design — `vb.register_biome`'s `decoration`
field only becomes a real schematic when it's a *table*, unchanged
behavior for every existing string usage.

Previous entry: Phase 6.13 — read-only server config visibility:
`PackRuntime::set_server_config(const core::ServerConfig&)`
(`inc/vb/script/pack_runtime.hpp`/`src/script/pack_runtime.cpp`) stores a
`PackRuntime::set_server_config(const core::ServerConfig&)`
(`inc/vb/script/pack_runtime.hpp`/`src/script/pack_runtime.cpp`) stores a
copy on `Impl` and backs a new `vb.config.get(key)` binding — deliberately
read-only, no setter exposed to Lua, per this item's own framing ("a pack
should not be able to silently change `max_players` out from under the
operator running the server"), unlike 6.6-6.11's override tables. Exposed
keys: `bind_address`, `port`, `content_pack`, `max_players`,
`view_distance`, `tick_rate`, `world_seed`, `gravity`, `void_kill_y`,
`day_length_seconds`, `asset_max_file_mb`, `asset_max_total_mb`,
`auth_mode` (as `"none"`/`"token"`), `motd`; any other key (or the whole
call, if `set_server_config` was never invoked) returns `nil`.
`src/server/main.cpp` calls it right after constructing `pack_runtime`,
before `load_content_pack`, so the value is visible even to registration-
time (module-scope) pack code. `--singleplayer`'s in-process `PackRuntime`
(`src/client/main.cpp`) has no `ServerConfig`/`server.toml` on that path
(same gap 6.7's entry already noted for physics params) and was left
un-wired — `vb.config.get` returns `nil` for every key there, which is
itself the documented/tested behavior, not an oversight. New tests in
`tests/unit/pack_runtime_test.cpp`: one sets a `ServerConfig` and checks
several keys plus an unknown-key `nil`; one never calls
`set_server_config` and checks every key comes back `nil`. Full `vb_tests`
green on `build-net-lua` (`VB_WITH_NET=ON`, `VB_WITH_LUA=ON`, 250/250
cases, up from 248). All 4 CTest cases (`vb_tests`/`server_smoke`/
`client_smoke`/`singleplayer_smoke`) pass. Not verified under a no-Lua/ASan
config this session (no pre-built ASan dir remains on this machine, see
`STATE.md.local`) — the new binding is entirely inside `pack_runtime.cpp`'s
`#if VB_WITH_LUA` region (the stub `set_server_config` is a no-op), so the
stub-build risk is low, just not re-confirmed here.
Previous entry: Phase 6.11 — item drop parameters (default +
override): `ItemDropSystem::spawn()` (`inc/vb/world/item_drops.hpp`/`.cpp`)
now takes optional per-drop `pickup_radius`/`lifetime_seconds`, falling back
to the system's own construction-time defaults (1.5 / 120.0) when unset —
every pre-6.11 caller/test is byte-identical. `BlockType` gains
`pickup_radius`/`drop_lifetime_seconds` (`inc/vb/world/block.hpp`, both
`-1.0` sentinel = "no override" since 0 is a real, if odd, radius value);
`vb.register_block{pickup_radius=..., item_lifetime_seconds=...}`
(`src/script/pack_runtime.cpp`) sets them — not `register_item`, same
"every holdable item is a registered block" reasoning 6.9 already
established. `ServerSession::spawn_item_drop` (`src/net/session.cpp`) is the
one new lookup site: it reads the dropped item's `BlockType` out of the live
`WorldReplicator`'s registry (falls through to the engine defaults when
there's no `replicator_` at all, e.g. a bare test harness, or the id isn't
registered) and threads any override into `item_drops_.spawn(...)`. New
tests: `tests/unit/item_drops_test.cpp` (a wide `pickup_radius` override
collects from outside the 1.5 system default; an overridden
`lifetime_seconds` survives well past the 120s default) and one
`pack_runtime_integration_test.cpp` end-to-end case (`register_block{
pickup_radius=10}` picked up 8 blocks away over a real `ServerSession`/
`ClientSession`/`LoopbackTransport`, following the item_drop_test's own
World-constructed-after-`freeze()` pattern so the new block name is actually
in the copy `World` holds — see this file's existing Phase 6.5 entry for why
that ordering matters). Full `vb_tests` green on `build-net-lua`
(`VB_WITH_NET=ON`, `VB_WITH_LUA=ON`, 248/248 cases). All 4 CTest cases
(`vb_tests`/`server_smoke`/`client_smoke`/`singleplayer_smoke`) pass. Not
verified under a no-Lua/ASan config this session (no pre-built ASan dir
remains on this machine) — the Lua binding is entirely inside
`pack_runtime.cpp`'s existing `#if VB_WITH_LUA` region;
`item_drops.{hpp,cpp}`/`block.hpp`/`session.cpp`'s changes have no Lua
dependency, so the stub-build risk is low, just not re-confirmed here.
Previous entry: Phase 6.10 — chat transform/moderation hook:
`net::ServerSession::ChatHookResult{veto, replacement_text}` replaces the old
bool-veto-only `set_chat_handler` signature — same veto-or-replace chaining
shape 6.3 already established for `player_input`/`InputHookResult`, just one
string field instead of a table. `PackRuntime::Impl::run_chat` (new, mirrors
`run_player_input`) chains every `vb.on("chat", handler)`: `false` vetoes
(first veto wins), a returned string replaces the text the *next* handler
sees (and, if it's the last to touch it, what gets broadcast), anything else
passes the current text through. `PackRuntime::dispatch_chat`'s return type
changed from `bool` to `ChatHookResult` to carry this — broke 6 existing
`CHECK(rt.dispatch_chat(...))` call sites in `pack_runtime_test.cpp` (that
file reuses the chat event purely as a generic "run this Lua and tell me if
it vetoed" RPC hook, not real chat), fixed to `CHECK_FALSE(...).veto`. No
wire message changed — this is a server-side hook contract only, no
`kEngineProtocolVersion` bump. New end-to-end test in
`pack_runtime_integration_test.cpp` ("pack script rewrites chat text before
it broadcasts") chains two handlers (uppercase, then append a tag) over a
real `ServerSession`/`ClientSession`/`LoopbackTransport` to prove the
replacement actually reaches the broadcast `S2C_Chat`, not just the binding
in isolation. `content/base/crafting.lua`'s existing chat handler
(veto-only, never returns a string) is unaffected. Rate limiting explicitly
still not implemented — deferred to whatever pack wants one, per the task
item's own framing; see `REMAINING_TASKS.md` 6.10. Full `vb_tests` green on
`build-net-lua` (`VB_WITH_NET=ON`, `VB_WITH_LUA=ON`); all 4 CTest cases
(`vb_tests`/`server_smoke`/`client_smoke`/`singleplayer_smoke`) pass. Not
verified under a no-Lua/ASan config this session (no pre-built ASan dir
remains on this machine, per 6.4's entry) — `run_chat` is entirely inside
`pack_runtime.cpp`'s existing `#if VB_WITH_LUA` region (stub branch returns
`ChatHookResult{}` unchanged), and `session.hpp`/`session.cpp`'s changes have
no Lua dependency, so the no-Lua stub-build risk is low, just not
re-confirmed here. Previous entry: Phase 6.9 — inventory stacking: `BlockType::max_stack`
(default `world::kDefaultMaxStackSize` = 64) + `vb.register_block{max_stack=N}`
override, same shape as 6.5's `max_damage`, not `register_item` (every holdable
item is already a registered block, see `content/base/blocks/planks.lua`). New
`PackRuntime::Impl::give_item()` combines into existing under-cap slots before
starting new ones; both `player:give()` and the item-pickup handler now share
it (previously two separate `push_back` call sites). See §8's newest entry for
detail. Previous entry: Phase 6.16 — client-local HUD mechanism:
`ui.define_hud(render_fn)` + a new `client.*` raw-state table
(`client.break_progress()`/`client.screen_size()`) let a pack render the
hold-to-break progress bar in Lua instead of hardcoded `DrawRectangle` calls
in `src/client/main.cpp` — "engine provides raw state, Lua deals with
presentation." Also fixed a real bug found while wiring it: `--singleplayer`
never loaded any `ui/*.lua` file at all (no asset sync on that path), so
every existing Lua UI screen was silently dead there, not just the new HUD.
See §8's newest entry for the full writeup. Previous entry: Phase 6.8 — day/night cycle curve:
`vb::world::DayNightCurve` (a generalized `vector<DayNightKeyframe>`
replacing the old fixed 4-stop gradient tables) + `vb.daynight.set_curve{...}`
+ `S2C_DayNightCurve` (id 51, `kEngineProtocolVersion` 14 → 15) so a pack's
custom sky gradient actually reaches the client; also wired
`day_length_seconds` to both `server.toml` and `vb.daynight.set_day_length(...)`,
the second half of this item's originally-scoped gap. See §8's newest entry
for the full writeup. Previous entry: Phase 6.7 — physics/movement parameters:
`vb.physics.set_params{...}` + `PackRuntime::effective_move_params()`,
`S2C_MoveParams` (id 50, `kEngineProtocolVersion` 13 → 14) so client
prediction mirrors the server's authoritative tunables instead of silently
drifting; also fixed a pre-existing bug found while wiring it — the client
never received the server's `MoveParams` at all before this, and two
`src/client/main.cpp` call sites were stomping it back to hardcoded defaults
right after join. See §8's newest entry for the full writeup. Previous
entry: Phase 6.5 — shared block-damage breaking:
`vb.register_block{max_damage=...}` + `vb::world::BlockDamageSystem` +
`C2S_BlockBreakBegin`/`Stop` + `vb.on("block_break_begin"/"block_break_tick"/
"block_health_tick", ...)`, completion drives the existing `C2S_BlockEdit`
pipeline; `kEngineProtocolVersion` 12 → 13. Crack-texture rendering and the
damage-value replication channel stay deferred (blocked on the texture/atlas
system, see `REMAINING_TASKS.md` 6.5) — see §8's newest entry for the full
design-scoping notes, including why re-registering an existing base block's
`max_damage` silently does nothing (`BlockRegistry::add_or_get` is
idempotent-by-name and never updates an existing entry's properties) and a
test-construction-order gotcha it cost debugging time to find. Previous
entry: Phase 6.4 — generic per-key persistent storage:
`vb.db.get/set/delete` + `vb.crypto.hash`, landed with a hand-rolled
`ScriptDb`/`sha256` instead of the SQLite backend `REMAINING_TASKS.md`
originally floated as the "leading candidate" — see §8's newest entry for
why and what's deferred if that swap is ever wanted. Note: the pre-built
`build-meshing/` ASan directory this file's §3 used to point at no longer
exists on this machine — only `build-net-lua/` remains; re-configure a fresh
ASan build from `cmake/Sanitizers.cmake` if that coverage is needed again.
Previous entry: 2026-09-17, Phase 6.1 — entity kinds as classes: `vb.register_entity`
+ `vb.world.spawn(kind, pos)` now really spawns something, with a persistent
per-instance `self` table and `on_spawn`/`on_tick`/`on_hit`/`on_death` all
wired up — following `ItemDropSystem`'s hardcoded-system-ahead-of-the-generic-
one precedent rather than the spec's EnTT-`ScriptState` design, since Phase
3.1's generic registry is still deferred. See §8's newest entry for the
design-deviation rationale and a test-writing gotcha (don't assert an exact
`self` value right after a spawn+pump — ticks already ran). Previous entry:
Phase 6.6 — player damage/death primitive: generic `player:damage(amount,
cause)` + a `vb.on("player_death", ...)` hook replace the hardcoded
instant-heal-and-teleport `check_respawns()`; `ServerSession` falls back to
the exact old behavior when no pack installs a handler, so every pre-6.6 test
still passes unmodified. See §8's Phase 6.6 entry for the
drop-inventory-at-death-not-respawn-position lesson learned while testing
it. Earlier: Phase 5.5 documentation pass complete —
`CONTRIBUTING.md` added (module map, build/test workflow, wire-message
checklist, Lua-binding guidelines); `docs/lua-api.md`/README's stale claims
fixed; see §8's fourteenth 2026-09-16 entry. Phase 5.1/4.3 — `--singleplayer` now runs the real
content pack: `Singleplayer` (`src/client/main.cpp`) rebuilt around a
directly-owned `LoopbackNetwork`/`ServerSession`/`ClientSession` instead of
`IntegratedGame`, so a real `PackRuntime` (scripting, chat, crafting, item
drops) and `HandshakeServerHost::block_registry` are both wired the same way
the dedicated server does them; see §8's thirteenth 2026-09-16 entry —
also documents a pre-existing, out-of-scope `server_smoke` failure mode
discovered along the way (`VB_WITH_COMPRESSION=ON` + a cwd with no
`content/base`). Phase 5.1 — simple crafting recipes, implemented
entirely as content per explicit user direction: `player:take()` is the only
new engine primitive, `content/base/crafting.lua` (a new generic top-level
pack module `load_content_pack` now knows to load) holds every actual game
rule; see §8's twelfth 2026-09-16 entry. Phase 5.1 — dropped-item entity: a real, working
`vb::world::ItemDropSystem` replicated through the existing interest-grid/
S2C_EntitySnapshot path (no new wire message), `vb.world.spawn_item_drop`,
`content/base/blocks/*.lua` on_break now drops into the world instead of
straight to inventory; see §8's eleventh 2026-09-16 entry. Phase 5.2 —
hold-to-break progress: LMB must be
held on the same voxel for a flat 0.35s before the edit is sent, plus a
screen-space progress bar; see §8's tenth 2026-09-16 entry. Phase 5.1 — real
inventory sync + basic hotbar:
`S2C_Inventory` (107), `PackRuntime::sync_inventory` pushed after
`player:give()`, `ClientSession::inventory()`, text-only HUD hotbar; see §8's
ninth 2026-09-16 entry — also corrected a stale 5.2 checklist entry claiming
the Lua block-edit veto was unwired, when it was actually already implemented.
Phase 5.4 complete — sfx hooks documented as not
implemented, `docs/lua-api.md`; see §8's eighth 2026-09-16 entry. Phase 5.4 —
death/respawn: void-kill Y threshold +
generic `health <= 0` respawn path, no new wire message (reuses `S2C_Chat`
privately); see §8's seventh 2026-09-16 entry. Phase 5.4 — day/night: `S2C_TimeOfDay`,
`vb::world::daynight.hpp` (pure tick/color math), sky-gradient `ClearBackground`
+ HH:MM overlay readout; see §8's sixth 2026-09-16 entry. Phase 5.4 — player
list / join-leave messages: `S2C_PlayerJoin`/`S2C_PlayerLeave`/`S2C_PlayerList`,
`ClientSession::players()`, top-right HUD list; see §8's fifth 2026-09-16
entry. Phase 5.4 — chat:
`C2S_Chat`/`S2C_Chat` wired end-to-end, `vb.on("chat")` veto, HUD chat box;
see §8's fourth 2026-09-16 entry. Phase 5.3 — main menu: `vb::render::MainMenu`
+ an `AppState` machine in `src/client/main.cpp` so the window opens before
any connection attempt in windowed mode; see §8's third 2026-09-16 entry)
