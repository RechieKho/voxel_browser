-- kitchen_sink:tundra -- see savanna.lua's comment for the shared context
-- (Phase 6.14 Voronoi-cell biome selection). Rarer than savanna
-- (`probability = 1.0` vs. its `2.0`) and, symmetrically, dislikes sitting
-- next to it. `surface`/`filler` use `base:sand` for a bleached, sparse
-- look distinct from savanna's grass -- purely a content choice, the engine
-- has no notion of "cold" biomes.
vb.register_biome({
	name = "kitchen_sink:tundra",
	surface = "base:sand",
	filler = "base:sand",
	stone = "base:stone",
	probability = 1.0,
	adjacency = { ["kitchen_sink:savanna"] = 0.05 },
})
