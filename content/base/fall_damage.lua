-- content/base/fall_damage.lua -- REMAINING_TASKS.md Phase 6's "no fall
-- damage" gap. vb.on("player_landed", ...) (Phase 6.22,
-- src/net/session.cpp's ServerSession -- fires once per player exactly on
-- the tick a fall is arrested by hitting ground) hands this file a raw
-- impact speed in m/s and nothing else -- the engine ships zero fall-damage
-- policy of its own, same "mechanism, not policy" split as punching/block
-- breaking in mechanics.lua.
--
-- Policy here: no damage below SAFE_SPEED (a short hop/step-off never hurts),
-- then 1 HP per m/s above that, matching player:damage()'s existing float
-- amount -- deliberately simple/linear, not Minecraft's per-block-of-fall
-- table, since this engine has no block-count fall history to draw from
-- (only the terminal impact speed).
local SAFE_SPEED = 8.0 -- m/s -- roughly a 3-block fall at this engine's gravity

vb.on("player_landed", function(player, impact_speed)
	local excess = impact_speed - SAFE_SPEED
	if excess > 0 then
		player:damage(excess, "fall")
	end
end)
