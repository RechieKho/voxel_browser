-- base:plains -- open grassland. A biome registered with vb.register_biome only
-- takes effect once a pack calls vb.worldgen.set_pipeline; content/base does in
-- worldgen.lua. `probability` is this biome's weight when each Voronoi cell
-- picks one (the larger, the more common).
vb.register_biome({
	name = "base:plains",
	surface = "base:grass",
	filler = "base:dirt",
	stone = "base:stone",
	probability = 3.0,
})
