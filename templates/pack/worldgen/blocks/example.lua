-- blocks/*.lua are loaded first, in file order. Block ids follow registration order and saved worlds
-- store ids, so append new blocks rather than reordering.
vb.register_block{
	name = "{{name}}:example",
	texture = "textures/example.png",
	-- max_damage = 3, -- punches needed to break (0 = instant)
}
