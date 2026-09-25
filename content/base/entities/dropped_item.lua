-- base:dropped_item -- registered for its visual only; content/base never
-- actually spawns one via vb.world.spawn. Real block drops go through a
-- separate, hardcoded `vb.world.spawn_item_drop` binding instead (see
-- blocks/*.lua's on_break), backed by `vb::world::ItemDropSystem`
-- (src/net/session.cpp) -- that system doesn't know or care about
-- pack-registered entity kinds in general, but `represents = "item_drop"`
-- below tells ServerSession to tag every real drop it spawns with this
-- kind's id anyway (see ServerSession::set_item_drop_visual_kind(),
-- PackRuntime::attach_session()), purely so EntityRenderer can find this
-- kind's `visual` for them. on_spawn/on_tick still never fire for a real
-- drop -- only for an instance a pack spawns itself via
-- vb.world.spawn("base:dropped_item", ...), same as any other kind (see
-- content/examples/kitchen_sink/entities/sentry.lua for a pack that does
-- that, Phase 6.15).
vb.register_entity({
	name = "base:dropped_item",
	represents = "item_drop",
	on_spawn = function(entity) end,
	on_tick = function(entity, dt) end,
	width = 0.4,
	height = 0.4,
	visual = {
		variant = "small",
		texture = "textures/dropped_item.png",
		facings = 4,
		clips = {
			{ clip = "idle", frames = 4, fps = 4.0 },
		},
	},
})
