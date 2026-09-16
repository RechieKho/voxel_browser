-- base:sand -- solid, opaque, found near sea level (see WorldGenerator).
base_sand_id = vb.register_block({
	name = "base:sand",
	solid = true,
	opaque = true,
	liquid = false,
	light = 0,
	on_break = function(ctx)
		vb.world.spawn_item_drop(
			{ x = ctx.pos.x + 0.5, y = ctx.pos.y + 0.5, z = ctx.pos.z + 0.5 },
			base_sand_id, 1)
	end,
})
