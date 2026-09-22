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

DayNightCurve default_day_night_curve() {
	// Same four keyframes (sunrise/noon/sunset/midnight) this file always
	// shipped, just expressed as one merged brightness+color curve instead of
	// two parallel stop tables -- Phase 6.8's vb.daynight.set_curve overrides
	// this same shape.
	return DayNightCurve{ {
			{ 0, 0.55, SkyColor{ 255, 170, 120 } }, // sunrise
			{ kTicksPerDay / 4, 1.0, SkyColor{ 135, 206, 235 } }, // noon
			{ kTicksPerDay / 2, 0.55, SkyColor{ 255, 130, 90 } }, // sunset
			{ (kTicksPerDay * 3) / 4, 0.08, SkyColor{ 12, 14, 34 } }, // midnight
	} };
}

namespace {

double lerp(double a, double b, double t) {
	return a + (b - a) * t;
}

std::uint8_t lerp_u8(std::uint8_t a, std::uint8_t b, double t) {
	return static_cast<std::uint8_t>(
			lerp(static_cast<double>(a), static_cast<double>(b), t));
}

// Brackets `ticks` between two consecutive keyframes of `curve` (wrapping
// past the last one back to the first at kTicksPerDay) and returns the
// interpolation fraction between them. `curve.keyframes` must be non-empty.
struct Bracket {
	const DayNightKeyframe *a;
	const DayNightKeyframe *b;
	double t;
};

Bracket bracket_for(const DayNightCurve &curve, std::uint32_t ticks) {
	ticks %= kTicksPerDay;
	const auto &kf = curve.keyframes;
	for (std::size_t i = 0; i < kf.size(); ++i) {
		const DayNightKeyframe &cur = kf[i];
		const DayNightKeyframe &next = kf[(i + 1) % kf.size()];
		const std::uint32_t next_tick =
				(i + 1 == kf.size()) ? kTicksPerDay : next.tick;
		if (ticks >= cur.tick && ticks < next_tick) {
			const double t = next_tick == cur.tick ? 0.0
												   : static_cast<double>(ticks - cur.tick) /
							static_cast<double>(next_tick - cur.tick);
			return { &cur, &next, t };
		}
	}
	return { &kf.front(), &kf.front(), 0.0 }; // unreachable given the loop above
}

} // namespace

double sky_brightness(std::uint32_t ticks, const DayNightCurve &curve) {
	if (curve.keyframes.empty()) {
		return sky_brightness(ticks, default_day_night_curve());
	}
	const Bracket b = bracket_for(curve, ticks);
	return lerp(b.a->brightness, b.b->brightness, b.t);
}

double sky_brightness(std::uint32_t ticks) {
	return sky_brightness(ticks, default_day_night_curve());
}

SkyColor sky_color_for_time(std::uint32_t ticks, const DayNightCurve &curve) {
	if (curve.keyframes.empty()) {
		return sky_color_for_time(ticks, default_day_night_curve());
	}
	const Bracket b = bracket_for(curve, ticks);
	return SkyColor{
		lerp_u8(b.a->color.r, b.b->color.r, b.t),
		lerp_u8(b.a->color.g, b.b->color.g, b.t),
		lerp_u8(b.a->color.b, b.b->color.b, b.t),
	};
}

SkyColor sky_color_for_time(std::uint32_t ticks) {
	return sky_color_for_time(ticks, default_day_night_curve());
}

} // namespace vb::world
