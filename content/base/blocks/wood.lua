-- base:wood -- solid, opaque log/trunk block. No real trees exist yet
-- (worldgen's decoration pass is deferred, REMAINING_TASKS.md 2.2/5.1) --
-- this block is placeable/breakable today the same way every other base
-- block is, it just never appears from natural generation.
base_wood_id = vb.register_block({
	name = "base:wood",
	solid = true,
	opaque = true,
	liquid = false,
	light = 0,
	on_break = function(ctx)
		vb.world.spawn_item_drop(
			{ x = ctx.pos.x + 0.5, y = ctx.pos.y + 0.5, z = ctx.pos.z + 0.5 },
			base_wood_id, 1)
	end,
})
