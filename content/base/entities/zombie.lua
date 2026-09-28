-- base:zombie -- REMAINING_TASKS.md's Phase 6 "No mob damage" gap. The
-- engine primitives (entity:damage()/player:damage()) already existed --
-- content/base simply never had a hostile mob using them.
--
-- **Real engine bug found while writing this, worked around here rather
-- than fixed (see STATE.md for the full investigation):** a PlayerHandle
-- stored across calls and read back from inside vb.on("tick", ...) or a
-- script entity's own on_tick returns garbage (wrong session pointer,
-- wrong net_id) -- calling a method on a *freshly passed-in* PlayerHandle
-- from within the SAME handler call is fine (every other content/base
-- script already does exactly that; mechanics.lua/fall_damage.lua never
-- store one), only *storing it for later, cross-call use specifically
-- from that dispatch path* is broken. So this mob's chase/attack logic is
-- driven from vb.on("player_input", ...) instead of the zombie's own
-- on_tick -- that handler hands over a fresh PlayerHandle on every real
-- input, used immediately, never stored. Only the zombie's own entity
-- handle (self:get_pos()/set_pos(), which never touches a PlayerHandle at
-- all) is stored across calls, keyed by the hunted player's name (the one
-- PlayerHandle field safe to snapshot as a plain string).
local kSpeed = 2.0 -- blocks/second, straight-line horizontal chase
local kAttackRange = 1.2
local kAttackDamage = 2.0
local kAttackCooldown = 1.0 -- seconds between bites once in range

vb.register_entity({
	name = "base:zombie",
	width = 0.6,
	height = 1.8,
	health = 20,
	on_spawn = function(self)
		self.attack_cooldown = 0.0
	end,
	on_death = function(self, cause)
		print(string.format("[base] zombie destroyed (cause='%s')", cause))
	end,
})

-- player name -> the zombie entity handle hunting them (vb.world.spawn's
-- own return value, the same "self" table on_tick would receive).
local hunting = {}

vb.on("player_input", function(player, input)
	local zombie = hunting[player:get_name()]
	if not zombie then
		return
	end
	zombie.attack_cooldown = math.max(0.0, zombie.attack_cooldown - input.dt)
	local target_pos = player:get_pos()
	local pos = zombie:get_pos()
	local dx = target_pos.x - pos.x
	local dz = target_pos.z - pos.z
	local dist = math.sqrt(dx * dx + dz * dz)
	if dist > kAttackRange then
		local step = math.min(kSpeed * input.dt, dist)
		local nx, nz = pos.x, pos.z
		if dist > 0 then
			nx = pos.x + dx / dist * step
			nz = pos.z + dz / dist * step
		end
		-- Horizontal chase only -- no pathfinding/jumping yet, so this
		-- also copies the target's own Y (a real pack would want real
		-- gravity/collision on the mob itself, out of scope here).
		zombie:set_pos(nx, target_pos.y, nz)
	elseif zombie.attack_cooldown <= 0.0 then
		player:damage(kAttackDamage, "zombie")
		zombie.attack_cooldown = kAttackCooldown
	end
end)

-- "/zombie" (spawn one 3m away, hunting the caller) -- the same
-- simplest-possible chat-command trigger sentry.lua/crafting.lua already
-- use, not a new UI or a real interact wire message.
vb.on("chat", function(player, text)
	if text ~= "/zombie" then
		return true
	end
	local pos = player:get_pos()
	local zombie = vb.world.spawn("base:zombie",
			{ x = pos.x + 3, y = pos.y, z = pos.z })
	hunting[player:get_name()] = zombie
	player:send_message("[base] a zombie is hunting you")
	return false
end)
