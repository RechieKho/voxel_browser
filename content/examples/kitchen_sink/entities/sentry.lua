-- kitchen_sink:sentry -- REMAINING_TASKS.md 6.15's "a custom entity kind
-- with on_tick/on_hit/on_death". `vb.register_entity` + `vb.world.spawn`
-- (Phase 6.1, landed 2026-09-17) are real and dispatched -- NOT the
-- content/base/entities/dropped_item.lua-era "captured, nothing calls them"
-- state (that file's own comment predates 6.1 and is now stale; not fixed
-- here since it's `content/base`, out of this pack's scope, but don't copy
-- its framing for new code). `on_spawn` fires once from `vb.world.spawn`;
-- `on_tick` fires once per server tick per live instance
-- (`PackRuntime::Impl::dispatch_entity_tick`); `on_hit` fires from
-- `self:damage(amount, cause)`; `on_death` fires from `self:remove(cause)`.
-- No EnTT registry backs any of this (Phase 3.1 still doesn't exist) -- it's
-- a small hardcoded system (`PackRuntime::Impl::entities`) replicated the
-- same way `vb.world.spawn_item_drop` already was, see 6.1's own
-- `REMAINING_TASKS.md` entry.
--
-- Also demonstrates 6.1's own noted gap deliberately left to packs: "no
-- automatic despawn-on-health-reaching-zero" -- `on_hit` below does that
-- itself, by calling `self:remove(cause)` once `self.hp` runs out.
vb.register_entity({
	name = "kitchen_sink:sentry",
	on_spawn = function(self)
		self.hp = 10
		self.age = 0
	end,
	on_tick = function(self, dt)
		self.age = self.age + dt
	end,
	on_hit = function(self, amount, cause)
		self.hp = self.hp - amount
		if self.hp <= 0 then
			self:remove(cause)
		end
	end,
	on_death = function(self, cause)
		print(string.format(
			"[kitchen_sink] sentry destroyed after %.1fs alive (cause='%s')",
			self.age, cause))
	end,
})

-- "/sentry" (spawn one at the caller's position, if none active), "/sentry
-- hit" (self:damage(4, "test") it -- three hits kills a fresh 10-hp one),
-- "/sentry kill" (self:remove("command") it outright) -- the simplest
-- possible trigger, same "/craft"-style chat-command choice `crafting.lua`
-- already makes, so this file doesn't also need a UI or a real interact
-- wire message (`player_interact` still has nothing sending it,
-- docs/lua-api.md). One global `instance`, not per-player, for simplicity --
-- a real pack would likely key this by player or by world position instead.
local instance = nil

vb.on("chat", function(player, text)
	local sub = text:match("^/sentry%s*(%a*)$")
	if not sub then
		return true
	end
	if sub == "" then
		if instance then
			player:send_message("[kitchen_sink] a sentry is already active")
		else
			local pos = player:get_pos()
			instance = vb.world.spawn("kitchen_sink:sentry", pos)
			player:send_message("[kitchen_sink] sentry spawned")
		end
	elseif sub == "hit" then
		if instance then
			instance:damage(4, "test")
			-- `instance` (the plain Lua `self` table) stays a valid table
			-- even after `on_hit` calls `self:remove()` internally -- only
			-- the engine-side entity is gone, so `instance.hp` (set by
			-- on_spawn/on_hit above) still reflects whether that happened.
			if instance.hp <= 0 then
				player:send_message("[kitchen_sink] sentry hit -- destroyed")
				instance = nil
			else
				player:send_message(string.format(
					"[kitchen_sink] sentry hit (%d hp left)", instance.hp))
			end
		else
			player:send_message("[kitchen_sink] no active sentry")
		end
	elseif sub == "kill" then
		if instance then
			instance:remove("command")
			instance = nil
			player:send_message("[kitchen_sink] sentry removed")
		else
			player:send_message("[kitchen_sink] no active sentry")
		end
	end
	return false
end)
