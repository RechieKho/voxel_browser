-- content/base's world generation: moves the base world onto the pack-driven
-- pipeline (`vb.worldgen.set_pipeline`, docs/lua-api.md) so its biomes
-- (biomes/plains.lua, biomes/forest.lua) take effect. Loaded after biomes/*.lua
-- (root-level files come after them), so both are already registered.
--
-- The terrain keeps the shape of the fixed default (same height range, sea
-- level and soil depth) but not its exact heights: a pipeline height field is
-- a vb.noise graph, so the noise is salted differently from the hardcoded
-- one. Worlds saved before this change keep their saved chunks and generate
-- new ones with this pipeline.
vb.worldgen.set_pipeline({
	-- Five octaves of value noise, like the fixed default. The source node
	-- runs at frequency 1 so the fbm node's own frequency sets the feature
	-- size (~96 blocks).
	height = vb.noise.fbm({
		source = vb.noise.value({ frequency = 1 }),
		octaves = 5,
		lacunarity = 2.0,
		gain = 0.5,
		frequency = 1 / 96,
	}),
	base_height = 64,
	amplitude = 28,
	sea_level = 62,
	soil_depth = 4,
	-- Sand at and just above the waterline, like the fixed default's beaches.
	beach = "base:sand",
	-- Voronoi biome cells are much larger than a chunk (32 blocks) so each
	-- biome reads as a region.
	cell_size = 256,
})

-- Decorative structures: structures/*.lua are data files written by the
-- structure editor (vb_structure_editor, docs/structure-editor.md), and
-- structures/all.lua returns every one of them. Biomes place them by name in
-- their `decoration` lists. Block names inside them resolve after the whole pack
-- has loaded, so this can sit here even though crafting.lua and others read block
-- ids earlier.
for _, structure in ipairs(require("structures.all")) do
	vb.register_structure(structure)
end
