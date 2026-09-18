-- kitchen_sink:savanna -- REMAINING_TASKS.md 6.15's "(once 6.9-6.11 land) a
-- stacking item, a chat filter, and tuned item-drop params" bullet plus
-- 6.14's own worked-example gap (`docs/lua-api.md`: "a worked pipeline
-- example belongs to 6.15's separate demo pack"). `vb.register_biome`
-- (Phase 6.14) gets a real consumer once worldgen.lua below calls
-- `vb.worldgen.set_pipeline` -- surface/filler/stone reference
-- `content/base`'s own block names by string (this pack never re-declares
-- them, but the registry a pack loads into always starts from
-- `BlockRegistry::base()`, so the names already resolve).
--
-- `probability`/`adjacency` are this biome's Voronoi-cell draw weighting
-- (`vb/worldgen/biome_selector.hpp`): savanna is twice as common as tundra
-- (see tundra.lua's own `probability`) and strongly dislikes sitting next to
-- it (a *soft* multiplier, not a hard 0 -- see `kAdjacencyFloor` in that
-- same header; tundra next to tundra stays neutral, the default 1.0 for any
-- unlisted pair).
vb.register_biome({
	name = "kitchen_sink:savanna",
	surface = "base:grass",
	filler = "base:dirt",
	stone = "base:stone",
	probability = 2.0,
	adjacency = { ["kitchen_sink:tundra"] = 0.05 },
})
