-- base:forest -- same status as biomes/plains.lua: declarative only, no
-- worldgen consumer yet. `decoration = "trees"` records intent for the
-- deferred decoration pass (REMAINING_TASKS.md 2.2).
vb.register_biome({
	name = "base:forest",
	surface = "base:grass",
	filler = "base:dirt",
	stone = "base:stone",
	decoration = "trees",
})
