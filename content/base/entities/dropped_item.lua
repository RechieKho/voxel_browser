-- base:dropped_item -- registered, but content/base never actually spawns
-- one. `vb.register_entity` + `vb.world.spawn(kind, pos)` (Phase 6.1, landed
-- 2026-09-17) are real and dispatched now -- on_spawn/on_tick/on_hit/
-- on_death all really fire, backed by a small hardcoded system
-- (`PackRuntime::Impl::entities`) rather than the EnTT registry §7.1
-- originally assumed (still doesn't exist, REMAINING_TASKS.md 3.1). See
-- `content/examples/kitchen_sink/entities/sentry.lua` (Phase 6.15) for a
-- pack that actually calls `vb.world.spawn`/`:damage`/`:remove`.
--
-- Real block drops go through a separate, hardcoded `vb.world.spawn_item_drop`
-- binding instead (see blocks/*.lua's on_break), backed by
-- `vb::world::ItemDropSystem` (src/net/session.cpp), not through this
-- `vb.register_entity` kind at all -- that system doesn't know or care about
-- pack-registered entity kinds; it's a fixed, working feature shipped ahead
-- of the general one, the same pattern Phase 2's worldgen and Phase 3.5's
-- placeholder billboards already used. This registration is kept as the
-- pack-format placeholder for whenever `content/base` itself wants its own
-- custom item-entity behavior (on_tick physics, despawn VFX, etc.) instead
-- of the fixed path.
vb.register_entity({
	name = "base:dropped_item",
	on_spawn = function(entity) end,
	on_tick = function(entity, dt) end,
})
