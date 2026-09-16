#include "vb/world/daynight.hpp"

#include <cmath>

namespace vb::world {

double advance_time_of_day(
		double current_ticks, double dt_seconds, double day_length_seconds) {
	if (day_length_seconds <= 0.0) {
		return current_ticks; // misconfigured; freeze rather than divide by zero
	}
	const double ticks_per_second =
			static_cast<double>(kTicksPerDay) / day_length_seconds;
	double next = current_ticks + ticks_per_second * dt_seconds;
	next = std::fmod(next, static_cast<double>(kTicksPerDay));
	if (next < 0.0) {
		next += static_cast<double>(kTicksPerDay);
	}
	return next;
}

namespace {

// Four keyframes spanning the day, wrapping back to the first at kTicksPerDay
// (same convention comment as the header: 0 sunrise, 1/4 noon, 1/2 sunset,
// 3/4 midnight).
struct ColorStop {
	std::uint32_t tick;
	SkyColor color;
};
constexpr ColorStop kColorStops[] = {
	{ 0, SkyColor{ 255, 170, 120 } }, // sunrise
	{ kTicksPerDay / 4, SkyColor{ 135, 206, 235 } }, // noon
	{ kTicksPerDay / 2, SkyColor{ 255, 130, 90 } }, // sunset
	{ (kTicksPerDay * 3) / 4, SkyColor{ 12, 14, 34 } }, // midnight
};
constexpr int kColorStopCount =
		static_cast<int>(sizeof(kColorStops) / sizeof(kColorStops[0]));

struct BrightnessStop {
	std::uint32_t tick;
	double value;
};
constexpr BrightnessStop kBrightnessStops[] = {
	{ 0, 0.55 }, // sunrise
	{ kTicksPerDay / 4, 1.0 }, // noon
	{ kTicksPerDay / 2, 0.55 }, // sunset
	{ (kTicksPerDay * 3) / 4, 0.08 }, // midnight
};
constexpr int kBrightnessStopCount = static_cast<int>(
		sizeof(kBrightnessStops) / sizeof(kBrightnessStops[0]));

double lerp(double a, double b, double t) {
	return a + (b - a) * t;
}

std::uint8_t lerp_u8(std::uint8_t a, std::uint8_t b, double t) {
	return static_cast<std::uint8_t>(
			lerp(static_cast<double>(a), static_cast<double>(b), t));
}

} // namespace

double sky_brightness(std::uint32_t ticks) {
	ticks %= kTicksPerDay;
	for (int i = 0; i < kBrightnessStopCount; ++i) {
		const BrightnessStop &cur = kBrightnessStops[i];
		const BrightnessStop &next = kBrightnessStops[(i + 1) % kBrightnessStopCount];
		const std::uint32_t next_tick =
				(i + 1 == kBrightnessStopCount) ? kTicksPerDay : next.tick;
		if (ticks >= cur.tick && ticks < next_tick) {
			const double t = static_cast<double>(ticks - cur.tick) /
					static_cast<double>(next_tick - cur.tick);
			return lerp(cur.value, next.value, t);
		}
	}
	return kBrightnessStops[0].value; // unreachable given the loop above
}

SkyColor sky_color_for_time(std::uint32_t ticks) {
	ticks %= kTicksPerDay;
	for (int i = 0; i < kColorStopCount; ++i) {
		const ColorStop &cur = kColorStops[i];
		const ColorStop &next = kColorStops[(i + 1) % kColorStopCount];
		const std::uint32_t next_tick =
				(i + 1 == kColorStopCount) ? kTicksPerDay : next.tick;
		if (ticks >= cur.tick && ticks < next_tick) {
			const double t = static_cast<double>(ticks - cur.tick) /
					static_cast<double>(next_tick - cur.tick);
			return SkyColor{
				lerp_u8(cur.color.r, next.color.r, t),
				lerp_u8(cur.color.g, next.color.g, t),
				lerp_u8(cur.color.b, next.color.b, t),
			};
		}
	}
	return kColorStops[0].color; // unreachable given the loop above
}

} // namespace vb::world
