-- A hand-written structure (docs/structure-editor.md section C). The format
-- is plain data: this file only returns a table, and worldgen.lua registers it
-- with `vb.register_structure(require("structures.acacia_tree"))`. The
-- structure editor reads and writes files of exactly this shape.
--
-- Rows are `size.x` characters, one row per z, one layer per y (bottom first).
-- "." keeps whatever terrain is already there, "W" is wood and "L" is leaves.
return {
	name = "kitchen_sink:acacia_tree",
	size = { x = 7, y = 6, z = 7 },
	anchor = { x = 3, y = 0, z = 3 }, -- the trunk's foot, on the ground block
	palette = {
		["."] = false,
		["W"] = "base:wood",
		["L"] = "base:leaves",
	},
	variants = {
		-- The common, wide-crowned shape.
		{
			weight = 3,
			layers = {
				{ ".......", ".......", ".......", "...W...", ".......", ".......", "......." },
				{ ".......", ".......", ".......", "...W...", ".......", ".......", "......." },
				{ ".......", ".......", ".......", "...W...", ".......", ".......", "......." },
				{ ".......", "..LLL..", ".LLLLL.", ".LLWLLL", ".LLLLL.", "..LLL..", "......." },
				{ ".......", "..LLL..", ".LLLLL.", ".LLLLL.", ".LLLLL.", "..LLL..", "......." },
				{ ".......", ".......", "..LLL..", "..LLL..", "..LLL..", ".......", "......." },
			},
		},
		-- A shorter, rarer variant.
		{
			weight = 1,
			layers = {
				{ ".......", ".......", ".......", "...W...", ".......", ".......", "......." },
				{ ".......", ".......", ".......", "...W...", ".......", ".......", "......." },
				{ ".......", "..LLL..", ".LLLLL.", ".LLWLL.", ".LLLLL.", "..LLL..", "......." },
				{ ".......", ".......", "..LLL..", "..LLL..", "..LLL..", ".......", "......." },
				{ ".......", ".......", ".......", ".......", ".......", ".......", "......." },
				{ ".......", ".......", ".......", ".......", ".......", ".......", "......." },
			},
		},
	},
	placement = {
		on = { "base:grass" },
		replace = "air_and_plants",
		rotate = true,
		mirror = true,
		min_spacing = 6,
		cluster = 0.6,
		-- worldgen.lua layers a high-frequency cellular noise on the terrain, so
		-- this demo pack is far rougher than a real biome: a 7x7 crown routinely
		-- spans 15+ blocks of height. A real pack would use a small value here.
		max_slope = 24,
	},
}
