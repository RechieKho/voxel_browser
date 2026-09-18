-- REMAINING_TASKS.md 6.15's "a custom keybind bound to an action" and "a
-- `ui.define` screen opened via `player:open_ui`" bullets, combined: pressing
-- the registered key opens `kitchen_sink:status` (ui/status.lua).
--
-- `vb.register_keybind(name) -> index` (Phase 6.3) declares a closed-schema
-- custom input slot -- `index` becomes a bit position in every `InputCmd`,
-- capped at 32 total registrations. The client side of actually *sending*
-- this keybind (binding it to a real key, `docs/lua-api.md`'s own noted gap:
-- "no base-pack/client UI wires these yet") isn't attempted here either --
-- this file is the server-side half: react to the bit once it's set,
-- whatever client eventually sets it.
vb.register_keybind("kitchen_sink:open_status")

-- `input.keybinds` (Phase 6.3) is level-triggered (true for as long as the
-- key is held), not an edge event -- this closure's `was_down` table (keyed
-- by player name, persisted across calls since every pack file shares one
-- Lua state) turns it into "just pressed this tick" so holding the key
-- doesn't reopen the screen every single tick.
local was_down = {}

vb.on("player_input", function(player, input)
	local name = player:get_name()
	local down = input.keybinds["kitchen_sink:open_status"] or false
	if down and not was_down[name] then
		player:open_ui("kitchen_sink:status", {})
	end
	was_down[name] = down
	-- nil: pass the input through unmodified -- this handler only reacts to
	-- state, it never changes movement/look.
end)
