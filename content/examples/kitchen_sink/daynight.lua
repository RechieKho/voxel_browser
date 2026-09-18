-- REMAINING_TASKS.md 6.15's "`vb.daynight.set_curve`/`set_day_length` for a
-- non-default sky". `set_curve` (Phase 6.8) replaces the engine's default
-- 4-keyframe gradient (`vb::world::default_day_night_curve()`) outright --
-- calling it at all means supplying every keyframe, not just the ones that
-- differ. `tick` is a position within one full `kTicksPerDay` (24000) cycle,
-- same `0` = sunrise / quarter-day = noon / half-day = sunset / three-
-- quarter-day = midnight convention `vb/world/daynight.hpp` documents. A
-- real pack would usually spread keyframes evenly across all four; this one
-- is deliberately lopsided (a long, saturated orange dusk held across two
-- keyframes) to make the override obviously visible rather than a subtle
-- recolor.
vb.daynight.set_curve({
	keyframes = {
		{ tick = 0, brightness = 0.5, color = { 255, 170, 90 } }, -- warm sunrise
		{ tick = 6000, brightness = 1.0, color = { 135, 206, 235 } }, -- bright noon sky blue
		{ tick = 12000, brightness = 0.6, color = { 255, 140, 40 } }, -- sunset: orange dusk begins
		{ tick = 15000, brightness = 0.6, color = { 255, 140, 40 } }, -- (held, not a quick fade)
		{ tick = 18000, brightness = 0.05, color = { 10, 10, 40 } }, -- midnight: near-black
	},
})

-- `set_day_length(seconds)` (Phase 6.8) overrides the real seconds one
-- full in-game day takes -- the operator's `server.toml` `day_length_seconds`
-- (default 1200.0) is the base this stacks on top of. A short cycle here so
-- the curve above is actually visible within a short play session rather
-- than requiring a 20-minute wait.
vb.daynight.set_day_length(180.0)
