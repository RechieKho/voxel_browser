-- base:sticks -- crafted from base:planks (see crafting.lua). Non-solid and
-- non-opaque: a stick isn't a real wall in any block game, but this engine
-- has no separate "held item" concept from "placeable block" yet (same
-- BlockId-space limitation planks.lua's comment explains), so `solid =
-- false` is the closest approximation to "this shouldn't behave like a
-- normal building block" available today -- it can still technically be
-- placed, it just won't collide with anything.
base_sticks_id = vb.register_block({
	name = "base:sticks",
	solid = false,
	opaque = false,
	liquid = false,
	light = 0,
	on_break = function(ctx)
		vb.world.spawn_item_drop(
			{ x = ctx.pos.x + 0.5, y = ctx.pos.y + 0.5, z = ctx.pos.z + 0.5 },
			base_sticks_id, 1)
	end,
})
