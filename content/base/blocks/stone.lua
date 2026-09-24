-- base:stone -- solid, opaque, the deep terrain fill block.
-- `texture` (real texture/atlas system): a pack-relative path synced to the
-- client over Asset Sync like any other pack file; the client decodes it and
-- packs it into its per-session texture atlas (vb::render::TextureAtlas).
base_stone_id = vb.register_block({
	name = "base:stone",
	solid = true,
	opaque = true,
	liquid = false,
	light = 0,
	texture = "textures/stone.png",
	on_break = function(ctx)
		vb.world.spawn_item_drop(
			{ x = ctx.pos.x + 0.5, y = ctx.pos.y + 0.5, z = ctx.pos.z + 0.5 },
			base_stone_id, 1)
	end,
})
