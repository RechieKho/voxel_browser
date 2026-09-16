-- base:dirt -- solid, opaque terrain fill block.
--
-- `vb.register_block` is idempotent by name (BlockRegistry::add_or_get,
-- src/world/block.cpp): this re-declares the same id the Phase 2 hardcoded
-- BlockRegistry::base() set already assigned, it doesn't create a new one.
--
-- `on_break` receives `{pos = {x,y,z}, player = <Player>}` after the block is
-- already gone server-side (spec §10.5). Giving the broken block back to the
-- player as an item is a real, working "drop" via `player:give` -- distinct
-- from returning a value from `on_break`, which `PackRuntime` still only
-- logs, not materializes (src/script/pack_runtime.cpp's
-- `on_block_edit_after`).
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
		ctx.player:give({ item = base_dirt_id, count = 1 })
	end,
})
