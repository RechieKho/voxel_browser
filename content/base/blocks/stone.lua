-- base:stone -- solid, opaque, the deep terrain fill block.
base_stone_id = vb.register_block({
	name = "base:stone",
	solid = true,
	opaque = true,
	liquid = false,
	light = 0,
	on_break = function(ctx)
		ctx.player:give({ item = base_stone_id, count = 1 })
	end,
})
