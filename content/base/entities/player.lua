-- base:player -- registered for its visual only; players never go through
-- vb.world.spawn (they join via the handshake, ServerSession::tick()'s own
-- join path). `represents = "player"` tells ServerSession to tag every
-- joining player's replicated EntityRecord with this kind's id (see
-- ServerSession::set_player_visual_kind(), PackRuntime::attach_session()),
-- purely so EntityRenderer can find this kind's `visual` for them --
-- on_spawn/on_tick/on_hit/on_death never fire for a real player, only for an
-- instance a pack spawns itself via vb.world.spawn("base:player", ...).
-- width/height match render::EntityRenderer's own placeholder billboard
-- dimensions (and physics::MoveParams' player collision height), so the
-- sprite's footprint lines up with the (unchanged) collision box.
vb.register_entity({
	name = "base:player",
	represents = "player",
	width = 0.8,
	height = 1.8,
	visual = {
		variant = "small",
		texture = "textures/player.png",
		facings = 4,
		origin = { x = 0.5, y = 1.0 },
		clips = {
			{ clip = "idle", frames = 2, fps = 2.0 },
			{ clip = "walk", frames = 4, fps = 6.0 },
		},
	},
})
