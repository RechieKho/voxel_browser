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
		vb.world.spawn_item_drop(
			{ x = ctx.pos.x + 0.5, y = ctx.pos.y + 0.5, z = ctx.pos.z + 0.5 },
			base_leaves_id, 1)
	end,
})
