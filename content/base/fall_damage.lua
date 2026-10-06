-- content/base/fall_damage.lua -- vb.on("player_landed", ...) (Phase 6.22,
-- src/net/session.cpp's ServerSession -- fires once per player exactly on
-- the tick a fall is arrested by hitting ground) hands this file a raw
-- impact speed in m/s and nothing else -- the engine ships zero fall-damage
-- policy of its own, same "mechanism, not policy" split as punching/block
-- breaking in mechanics.lua.
--
-- Policy: measured in blocks fallen, not speed. A fall of SAFE_BLOCKS or
-- fewer never hurts; each block beyond that costs 1 HP. The height is
-- recovered from the impact speed with v^2 = 2 * g * h, where g is the
-- engine's *falling* gravity (gravity * fall_gravity_scale, both read back
-- from vb.physics.get_params() inside the handler -- a pack can call
-- vb.physics.set_params() at any point during load, so this file must not
-- cache them at load time).
--
-- A plain jump rises only ~1.3 blocks, far under SAFE_BLOCKS, so no jump-speed
-- margin is needed any more.
--
-- session.cpp captures impact speed one tick *before* the landing tick's own
-- gravity step, so the reported speed is up to one tick of falling gravity
-- low; TICK_SECONDS adds that back so a fall of exactly SAFE_BLOCKS is not
-- read as slightly shorter than it was.
local SAFE_BLOCKS = 6.0
local TICK_SECONDS = 1.0 / 20.0 -- default server tick_rate

vb.on("player_landed", function(player, impact_speed)
	local params = vb.physics.get_params()
	local fall_gravity = params.gravity * params.fall_gravity_scale
	local speed = impact_speed + fall_gravity * TICK_SECONDS
	local blocks = speed * speed / (2.0 * fall_gravity)
	local excess = blocks - SAFE_BLOCKS
	if excess > 0 then
		player:damage(excess, "fall")
	end
end)
