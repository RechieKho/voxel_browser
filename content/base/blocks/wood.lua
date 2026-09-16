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
		ctx.player:give({ item = base_wood_id, count = 1 })
	end,
})
