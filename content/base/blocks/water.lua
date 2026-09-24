-- base:water -- non-solid, non-opaque, flowing liquid (region-tracked).
--
-- Unlike every other file in this directory, `base:water` doesn't originate
-- here: `BlockRegistry::base()` (src/world/block.cpp) already hardcodes it
-- as part of the Phase 2 fallback set (solid=false, opaque=false, liquid=
-- true, region=true -- the first user of Phase 7.3's generic per-tick
-- occupancy hook). `vb.register_block` is idempotent by name
-- (BlockRegistry::add_or_get), so this re-declares the exact same id and
-- props purely to attach a real `texture` -- the one thing the hardcoded
-- C++ default can't carry (no Lua/asset-sync path from there).
vb.register_block({
	name = "base:water",
	solid = false,
	opaque = false,
	liquid = true,
	light = 0,
	region = true,
	texture = "textures/water.png",
})
