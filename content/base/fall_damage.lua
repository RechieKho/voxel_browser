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
--
-- SAFE_SPEED must clear a plain jump's own landing speed, or jumping on flat
-- ground hurts (found as a bug: a hardcoded 8.0 sat *below* the engine
-- default jump_speed of 8.9 m/s, and gravity is symmetric -- a jump lands at
-- about the speed it launched at). Read jump_speed back from the engine
-- (vb.physics.get_params(), Phase 6.21) instead of a second hardcoded guess
-- that could drift out of sync with it again -- called inside the handler,
-- like mechanics.lua's own get_params() uses, since a pack can call
-- vb.physics.set_params() at any point during load and this file's load
-- order relative to that isn't guaranteed. Plus a margin: session.cpp
-- captures impact speed one tick *before* the landing tick's own gravity
-- step, so the real impact is up to one tick of gravity (params.gravity *
-- dt) higher than what player_landed reports.
vb.on("player_landed", function(player, impact_speed)
	local safe_speed = vb.physics.get_params().jump_speed + 1.5
	local excess = impact_speed - safe_speed
	if excess > 0 then
		player:damage(excess, "fall")
	end
end)
