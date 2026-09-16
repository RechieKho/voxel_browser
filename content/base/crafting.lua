-- Simple crafting: wood -> planks -> sticks (REMAINING_TASKS.md 5.1's
-- suggested example). This is the reference example for `vb.register_craft`
-- and `player:take()` -- the engine (src/script/pack_runtime.cpp) has no
-- idea what a "recipe" is, what order recipes apply in, or how a player
-- triggers one; every one of those decisions is made right here, in
-- content. A different pack is free to ignore this file's approach entirely
-- (a crafting-table UI, a different command syntax, automatic crafting on
-- pickup, no crafting at all) without touching a single line of engine code.
--
-- Loaded by src/script/pack_loader.cpp as a generic top-level pack module --
-- after blocks/*.lua (so base_wood_id/base_planks_id/base_sticks_id already
-- exist as globals) but before init.lua.

-- The actual recipe data lives in a plain Lua table, keyed by recipe name --
-- this is what the command handler below reads. `vb.register_craft` is also
-- called for each one purely so the engine-side registry (currently
-- write-only, REMAINING_TASKS.md 5.1: "vb.register_craft remains captured
-- with zero consumer") has a record of what exists, for whenever a future
-- system (a crafting-table UI's item grid, 4.5's still-missing widget) wants
-- to read it back rather than reparsing this file.
local recipes = {}

local function define_recipe(name, inputs, output)
	recipes[name] = { inputs = inputs, output = output }
	vb.register_craft({ name = name, inputs = inputs, output = output })
end

define_recipe("base:planks",
	{ { item = base_wood_id, count = 1 } },
	{ item = base_planks_id, count = 4 })

define_recipe("base:sticks",
	{ { item = base_planks_id, count = 2 } },
	{ item = base_sticks_id, count = 4 })

local function count_item(player, item_id)
	local total = 0
	for _, stack in ipairs(player:get_inventory()) do
		if stack.item == item_id then
			total = total + stack.count
		end
	end
	return total
end

-- "/craft <name>" over chat -- the simplest possible trigger, chosen so this
-- example doesn't also need to invent a crafting-table UI screen. Vetoes
-- (returns false for) any message that starts with "/craft " so the raw
-- command text never gets broadcast as a chat line, whether or not the
-- recipe actually succeeds; anything else passes through untouched.
vb.on("chat", function(player, text)
	local recipe_name = text:match("^/craft%s+(%S+)$")
	if not recipe_name then
		return true
	end

	local recipe = recipes[recipe_name]
	if not recipe then
		player:send_message("Unknown recipe: " .. recipe_name)
		return false
	end

	for _, req in ipairs(recipe.inputs) do
		if count_item(player, req.item) < req.count then
			player:send_message("Missing ingredients for " .. recipe_name)
			return false
		end
	end

	-- All-or-nothing: every input already confirmed sufficient above, so
	-- each take() below is expected to succeed -- player:take() (added
	-- alongside this file) returns false instead of throwing if a caller
	-- gets that wrong, so a bug here fails loud in testing rather than
	-- corrupting an inventory mid-craft.
	for _, req in ipairs(recipe.inputs) do
		assert(player:take({ item = req.item, count = req.count }))
	end
	player:give(recipe.output)
	player:send_message("Crafted " .. recipe_name)
	return false
end)
