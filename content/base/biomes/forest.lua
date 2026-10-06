-- base:forest -- same shape as base:plains, rarer. `decoration = "trees"` is the
-- pre-structure placeholder; real decoration entries arrive with the
-- structures (see structures/).
vb.register_biome({
	name = "base:forest",
	surface = "base:grass",
	filler = "base:dirt",
	stone = "base:stone",
	probability = 2.0,
	decoration = "trees",
})
