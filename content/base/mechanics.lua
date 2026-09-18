-- content/base/mechanics.lua -- Growtopia-style discrete punching, in Lua.
--
-- REMAINING_TASKS.md 6.17/6.18: combat used to be Minecraft-style
-- hold-to-break (a fixed 0.35s LMB-hold timer, 100% hardcoded C++, no pack
-- involvement, and no way to hit another player at all). The engine now has
-- zero built-in "holding a button does X" behavior -- src/client/main.cpp
-- only ever reports raw, level-triggered button state (input.buttons.primary
-- = "LMB is down right now", every tick, exactly like buttons.jump/sprint);
-- this file turns the *rising edge* of that into one discrete punch via
-- player:punch(), and the engine decides what the punch actually hit.
--
-- player:punch() (Phase 6.18, src/net/session.cpp's ServerSession::punch())
-- raycasts blocks and nearby players along this player's own authoritative
-- look direction and resolves to whichever is closer -- a block accumulates
-- one hit against BlockType::max_damage (0 = breaks on the first punch,
-- same as every pre-6.18 block); a player takes vb.combat.set_params's
-- default PvP damage. Neither of those policies lives here -- this file's
-- only job is deciding *when* a punch happens (once per click, not once per
-- tick a mouse button happens to still be down).

-- Per-player "was primary already down last tick" edge state, keyed by
-- player name -- same convention content/examples/kitchen_sink/keybinds.lua
-- already uses (input.keybinds/buttons are level-triggered; PlayerHandle is
-- a fresh wrapper object every call, not a stable table key, so a NetId-or-
-- name string is the only stable thing to index by).
local was_down = {}

vb.on("player_input", function(player, input)
	local name = player:get_name()
	local down = input.buttons and input.buttons.primary or false
	if down and not was_down[name] then
		player:punch()
	end
	was_down[name] = down
	-- nil: pass the input through unmodified -- this handler only reacts to
	-- state, it never changes movement/look.
end)
