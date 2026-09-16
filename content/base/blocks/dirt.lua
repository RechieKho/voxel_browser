-- base:dirt -- solid, opaque terrain fill block.
--
-- `vb.register_block` is idempotent by name (BlockRegistry::add_or_get,
-- src/world/block.cpp): this re-declares the same id the Phase 2 hardcoded
-- BlockRegistry::base() set already assigned, it doesn't create a new one.
--
-- `on_break` receives `{pos = {x,y,z}, player = <Player>}` after the block is
-- already gone server-side (spec §10.5). The broken block drops as a real
-- world item entity (`vb.world.spawn_item_drop`, backed by
-- vb::world::ItemDropSystem, src/net/session.cpp) instead of going straight
-- into the breaking player's inventory -- a player walking within pickup
-- range (including the one who broke it) auto-collects it, same as any
-- other player who happens to be standing there. Distinct from returning a
-- value from `on_break`, which `PackRuntime` still only logs, not
-- materializes (src/script/pack_runtime.cpp's `on_block_edit_after`).
-- A plain (non-local) global: every pack file loaded by
-- src/script/pack_loader.cpp shares one Lua state, so later files
-- (blocks/grass.lua, loaded second -- alphabetical order) can read this back
-- without needing a `vb.world.find_block(name)` lookup API, which doesn't
-- exist yet.
base_dirt_id = vb.register_block({
	name = "base:dirt",
	solid = true,
	opaque = true,
	liquid = false,
	light = 0,
	on_break = function(ctx)
		vb.world.spawn_item_drop(
			{ x = ctx.pos.x + 0.5, y = ctx.pos.y + 0.5, z = ctx.pos.z + 0.5 },
			base_dirt_id, 1)
	end,
})
