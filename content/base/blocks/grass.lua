-- base:grass -- solid, opaque surface block. Breaking it drops dirt (the
-- classic block-game convention), not grass itself -- there's no separate
-- "grass block item" concept here, just the terrain block it decays to.
-- Reads `base_dirt_id`, a global set by blocks/dirt.lua, which the sorted
-- load order (src/script/pack_loader.cpp) always loads first.
vb.register_block({
	name = "base:grass",
	solid = true,
	opaque = true,
	liquid = false,
	light = 0,
	on_break = function(ctx)
		ctx.player:give({ item = base_dirt_id, count = 1 })
	end,
})
