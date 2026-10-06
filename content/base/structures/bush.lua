-- base:bush. Written by vb_structure_editor.
-- The layout is stable: hand edits that keep it valid load fine, but the editor
-- rewrites the whole file when it saves.
return {
	name = "base:bush",
	size = { x = 5, y = 4, z = 5 },
	anchor = { x = 2, y = 0, z = 2 },
	palette = {
		["."] = false,
		["L"] = "base:leaves",
		["W"] = "base:wood",
	},
	variants = {
		{
			weight = 1,
			layers = {
				{ -- y = 0
					".....",
					".LLL.",
					".LWL.",
					".LLL.",
					".....",
				},
				{ -- y = 1
					".LLL.",
					"LLLLL",
					"LLLLL",
					"LLLLL",
					".LLL.",
				},
				{ -- y = 2
					".....",
					".LL..",
					".LLL.",
					".LLL.",
					".....",
				},
				{ -- y = 3
					".....",
					".....",
					".....",
					".....",
					".....",
				},
			},
		},
		{
			weight = 1,
			layers = {
				{ -- y = 0
					".....",
					".LLL.",
					".LWL.",
					".LLL.",
					".....",
				},
				{ -- y = 1
					".LLL.",
					"LL.LL",
					"LL.LL",
					"LLLLL",
					".LLL.",
				},
				{ -- y = 2
					".....",
					".LLL.",
					".LLL.",
					".LLL.",
					".....",
				},
				{ -- y = 3
					".....",
					".....",
					".....",
					".....",
					".....",
				},
			},
		},
		{
			weight = 1,
			layers = {
				{ -- y = 0
					".....",
					".LLL.",
					".LWL.",
					".LLL.",
					".....",
				},
				{ -- y = 1
					".LLL.",
					"LLLLL",
					"LLLLL",
					"LLLLL",
					".LLL.",
				},
				{ -- y = 2
					".....",
					".LLL.",
					".LLL.",
					".LLL.",
					".....",
				},
				{ -- y = 3
					".....",
					".....",
					".....",
					".....",
					".....",
				},
			},
		},
	},
	placement = {
		on = { "base:grass", "base:dirt" },
		replace = "air_and_plants",
		rotate = true,
		mirror = true,
		min_spacing = 4,
		cluster = 0.4,
		max_slope = 2,
	},
}
