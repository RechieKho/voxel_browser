-- base:dropped_item -- declarative only, like the biomes above.
-- `vb.register_entity` (src/script/pack_runtime.cpp) captures on_spawn/
-- on_tick/on_hit/on_death and returns a stable EntityKindId, but nothing
-- ever calls them: there's no EnTT registry driving generic entities yet
-- (REMAINING_TASKS.md 3.1 -- the server still only simulates players
-- directly), so `vb.world.spawn("base:dropped_item", pos)` just logs and
-- returns nil (world_tbl["spawn"] in pack_runtime.cpp). Kept here as the
-- pack-format placeholder for whenever that lands; real drops in this pack
-- go straight into the breaking player's inventory instead (see
-- blocks/*.lua's on_break), not through a dropped-item entity in the world.
vb.register_entity({
	name = "base:dropped_item",
	on_spawn = function(entity) end,
	on_tick = function(entity, dt) end,
})
