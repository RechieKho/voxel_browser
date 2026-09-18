-- REMAINING_TASKS.md 6.15's "`vb.physics.set_params` tuning movement".
-- `vb.physics.set_params` (Phase 6.7, `docs/lua-api.md`) overrides only the
-- fields set here -- every other `physics::MoveParams` field (friction,
-- step_height, half_width, ...) keeps the operator's `server.toml` default.
-- A pack-load-time-only call (rejected once the registry freezes), like
-- every other `set_*` override in this pack.
vb.physics.set_params({
	gravity = 18.0, -- floatier than the engine's own 28.0 m/s^2 default
	jump_speed = 9.0, -- correspondingly higher jump to match
	sprint_speed = 9.5, -- noticeably faster than the 7.0 m/s default
})
