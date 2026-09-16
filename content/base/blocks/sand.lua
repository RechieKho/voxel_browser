-- base:sand -- solid, opaque, found near sea level (see WorldGenerator).
base_sand_id = vb.register_block({
	name = "base:sand",
	solid = true,
	opaque = true,
	liquid = false,
	light = 0,
	on_break = function(ctx)
		ctx.player:give({ item = base_sand_id, count = 1 })
	end,
})
