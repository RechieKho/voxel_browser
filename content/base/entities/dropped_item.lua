-- base:dropped_item -- still declarative only, like the biomes above.
-- `vb.register_entity` (src/script/pack_runtime.cpp) captures on_spawn/
-- on_tick/on_hit/on_death and returns a stable EntityKindId, but nothing
-- ever calls them: there's no EnTT registry driving generic Lua entity
-- kinds yet (REMAINING_TASKS.md 3.1 -- the server still only simulates
-- players directly), so `vb.world.spawn("base:dropped_item", pos)` still
-- just logs and returns nil (world_tbl["spawn"] in pack_runtime.cpp).
--
-- Real block drops DO now spawn a genuine world item entity (see
-- blocks/*.lua's on_break) -- but through a separate, hardcoded
-- `vb.world.spawn_item_drop` binding backed by vb::world::ItemDropSystem
-- (src/net/session.cpp), not through this `vb.register_entity` kind at
-- all. That system doesn't know or care about pack-registered entity kinds;
-- it's a fixed, working feature shipped ahead of the general one, the same
-- pattern Phase 2's worldgen and Phase 3.5's placeholder billboards already
-- used. This registration is kept purely as the pack-format placeholder for
-- whenever a real EnTT-backed spawn exists and a pack might want its own
-- custom item-entity behavior (on_tick physics, despawn VFX, etc.).
vb.register_entity({
	name = "base:dropped_item",
	on_spawn = function(entity) end,
	on_tick = function(entity, dt) end,
})
