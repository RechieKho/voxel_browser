-- content/base/keybinds.lua -- wires the two screens ui/pause.lua and
-- ui/inventory.lua were left with "nothing opens this yet" (see their own
-- header comments and REMAINING_TASKS.md 7.4). `player:open_ui()` is
-- server-push-only, so a client gesture has to ask for it via a real input
-- signal -- the same `vb.register_keybind` + `vb.on("player_input")` rising-
-- edge pattern `content/examples/kitchen_sink/keybinds.lua` already
-- demonstrates for a custom screen, applied here to content/base's own.
--
-- The other half of this -- mapping "base:pause"/"base:inventory" to a real
-- physical key -- lives client-side in src/client/main.cpp's
-- `kCustomKeybinds` table (Escape/E respectively), since `input.keybinds`
-- here is just a named bit with no physical key behind it until a client
-- chooses to set one.
vb.register_keybind("base:pause")
vb.register_keybind("base:inventory")

-- Level-triggered input.keybinds turned into "just pressed this tick", same
-- convention as kitchen_sink/keybinds.lua and mechanics.lua's own
-- was_down tables -- holding the key must not reopen the screen every tick.
local was_pause_down = {}
local was_inventory_down = {}

vb.on("player_input", function(player, input)
	local name = player:get_name()

	local pause_down = input.keybinds["base:pause"] or false
	if pause_down and not was_pause_down[name] then
		player:open_ui("base:pause", {})
	end
	was_pause_down[name] = pause_down

	local inventory_down = input.keybinds["base:inventory"] or false
	if inventory_down and not was_inventory_down[name] then
		player:open_ui("base:inventory", { slots = player:get_inventory() })
	end
	was_inventory_down[name] = inventory_down
	-- nil: pass the input through unmodified -- this handler only reacts to
	-- state, it never changes movement/look.
end)
