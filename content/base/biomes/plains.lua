-- base:plains -- declarative only. `vb.register_biome` (src/script/
-- pack_runtime.cpp) captures this table but nothing reads it back yet: the
-- server's WorldGenerator is still the Phase 2 hardcoded fBm-heightmap
-- pipeline (src/worldgen/generator.cpp), not the Lua-driven
-- `vb.worldgen.set_pipeline` one the spec describes (§10.3) --
-- REMAINING_TASKS.md 4.2/2.2 track that swap as a separate, unstarted
-- follow-up. Kept here as the pack-format placeholder for when it lands.
vb.register_biome({
	name = "base:plains",
	surface = "base:grass",
	filler = "base:dirt",
	stone = "base:stone",
})
