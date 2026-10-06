-- Root *.lua files load after blocks/ entities/ biomes/ and before init.lua.
-- Terrain is data, compiled once after the pack loads (it runs on worker threads, so no callbacks).

vb.register_biome{
	name = "{{name}}:plains",
	surface = "base:grass",
	filler = "base:dirt",
	stone = "base:stone",
}

vb.worldgen.set_pipeline{
	-- Required: a vb.noise.* node. fbm = layered noise; frequency is 1 / feature size in blocks.
	height = vb.noise.fbm{ source = vb.noise.value(), octaves = 4, frequency = 1 / 96 },
	base_height = 64,
	amplitude = 20,
	sea_level = 60,
	soil_depth = 4,
	beach = "base:sand",
	cell_size = 192, -- Voronoi biome cell size in blocks
}
