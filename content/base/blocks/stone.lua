-- base:stone -- solid, opaque, the deep terrain fill block.
base_stone_id = vb.register_block({
	name = "base:stone",
	solid = true,
	opaque = true,
	liquid = false,
	light = 0,
	on_break = function(ctx)
		vb.world.spawn_item_drop(
			{ x = ctx.pos.x + 0.5, y = ctx.pos.y + 0.5, z = ctx.pos.z + 0.5 },
			base_stone_id, 1)
	end,
})
