-- base:forest -- same ground as base:plains, much denser with trees.
vb.register_biome({
	name = "base:forest",
	surface = "base:grass",
	filler = "base:dirt",
	stone = "base:stone",
	probability = 2.0,
	decoration = {
		{ structure = "base:oak_tree", spawn_rate = 5.0 },
		{ structure = "base:birch_tree", spawn_rate = 2.5 },
		{ structure = "base:bush", spawn_rate = 3.0 },
		{ structure = "base:boulder", spawn_rate = 0.3 },
	},
})
