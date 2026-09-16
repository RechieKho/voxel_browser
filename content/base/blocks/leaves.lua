-- base:leaves -- solid but non-opaque (BlockRegistry::base() already has it
-- this way, src/world/block.cpp) so light passes through and the mesher
-- doesn't cull faces against it the way it does for stone/dirt/etc.
base_leaves_id = vb.register_block({
	name = "base:leaves",
	solid = true,
	opaque = false,
	liquid = false,
	light = 0,
	on_break = function(ctx)
		ctx.player:give({ item = base_leaves_id, count = 1 })
	end,
})
