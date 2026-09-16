#pragma once

#include <cstdint>

// Day/night cycle (spec §5.4): the server owns a single `time_of_day` clock
// (ticks into the day, wrapping at kTicksPerDay) advanced once per tick and
// replicated to clients via S2C_JoinAccept (initial value) + S2C_TimeOfDay
// (periodic updates). This header is pure/no-raylib so it's unit-testable
// like vb/render/camera.hpp; the client converts a tick value to an actual
// raylib Color itself (src/client/main.cpp), not here.

namespace vb::world {

// Ticks in a full day/night cycle. 0 = sunrise, kTicksPerDay/4 = noon,
// kTicksPerDay/2 = sunset, 3*kTicksPerDay/4 = midnight -- same convention as
// the doc comment on protocol::S2CJoinAccept::time_of_day.
inline constexpr std::uint32_t kTicksPerDay = 24000;

// Advances `current_ticks` by `dt_seconds` worth of in-game time (a full day
// takes `day_length_seconds` real seconds) and wraps into [0, kTicksPerDay).
// Kept as a free function (not a class) since ServerSession already owns the
// single double accumulator that needs this each tick.
double advance_time_of_day(
		double current_ticks, double dt_seconds, double day_length_seconds);

// Sky brightness at a given time of day, 0 (fully dark, midnight) to 1 (fully
// bright, noon). Exposed separately from sky_color_for_time() so callers that
// just need e.g. ambient light scaling don't need an RGB triple.
double sky_brightness(std::uint32_t ticks);

struct SkyColor {
	std::uint8_t r = 0;
	std::uint8_t g = 0;
	std::uint8_t b = 0;
};

// A simple 4-keyframe (sunrise/noon/sunset/midnight) gradient over the day
// cycle -- "simple sky gradient" per the spec, not a physically based sky.
SkyColor sky_color_for_time(std::uint32_t ticks);

} // namespace vb::world
