-- kitchen_sink:sentry -- REMAINING_TASKS.md 6.15's "a custom entity kind
-- with on_tick/on_hit/on_death". Same honest limitation
-- `content/base/entities/dropped_item.lua` already documents: no EnTT
-- registry drives generic Lua entity kinds yet (REMAINING_TASKS.md 3.1 --
-- the server still only simulates players directly), so
-- `vb.world.spawn("kitchen_sink:sentry", pos)` logs and returns nil, and
-- none of the three callbacks below ever actually fires today. Registered
-- here anyway, with real (if inert) bodies, as the pack-format reference for
-- when a real spawn path exists -- copy this file's shape rather than
-- guessing at the callback signatures from `ARCHITECTURE_SPEC.md` alone.
vb.register_entity({
	name = "kitchen_sink:sentry",
	on_spawn = function(entity) end,
	-- `dt` is the same per-tick delta every other Phase 6 tick hook receives
	-- (PackRuntime::dispatch_tick). A real sentry would look for nearby
	-- players here and decide whether to aggro.
	on_tick = function(entity, dt) end,
	-- `amount`/`cause` mirror `player:damage(amount, cause?)`'s own shape
	-- (Phase 6.6) -- a real sentry would reduce its own health here and
	-- despawn once it runs out (REMAINING_TASKS.md 6.1's own noted gap: "no
	-- automatic despawn-on-health-reaching-zero for non-player entities").
	on_hit = function(entity, amount, cause) end,
	on_death = function(entity, cause) end,
})
