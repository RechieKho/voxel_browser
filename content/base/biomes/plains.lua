-- base:plains -- open grassland with the odd tree, bush and boulder.
-- A biome registered with vb.register_biome only takes effect once a pack calls
-- vb.worldgen.set_pipeline; content/base does in worldgen.lua. `probability` is
-- this biome's weight when each Voronoi cell picks one (the larger, the more
-- common). Each `decoration` entry names a structure registered in
-- worldgen.lua; `spawn_rate` is the expected number of placements per 32x32
-- column, and any placement field of the structure (on, min_spacing, ...) could
-- be overridden here too.
vb.register_biome({
	name = "base:plains",
	surface = "base:grass",
	filler = "base:dirt",
	stone = "base:stone",
	probability = 3.0,
	decoration = {
		{ structure = "base:oak_tree", spawn_rate = 0.4 },
		{ structure = "base:bush", spawn_rate = 1.2 },
		{ structure = "base:boulder", spawn_rate = 0.15 },
	},
})
