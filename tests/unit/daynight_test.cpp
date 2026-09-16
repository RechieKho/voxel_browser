#include <doctest/doctest.h>

#include <cstdlib>

#include "vb/world/daynight.hpp"

using namespace vb::world;

TEST_CASE("advance_time_of_day: rate matches day_length_seconds") {
	// 1200s/day -> 24000/1200 = 20 ticks/sec.
	CHECK(advance_time_of_day(0.0, 1.0, 1200.0) == doctest::Approx(20.0));
	CHECK(advance_time_of_day(100.0, 2.0, 1200.0) == doctest::Approx(140.0));
}

TEST_CASE("advance_time_of_day: wraps at kTicksPerDay") {
	const double wrapped = advance_time_of_day(
			static_cast<double>(kTicksPerDay) - 10.0, 1.0, 1200.0);
	CHECK(wrapped == doctest::Approx(10.0));
	CHECK(wrapped >= 0.0);
	CHECK(wrapped < static_cast<double>(kTicksPerDay));
}

TEST_CASE("advance_time_of_day: a misconfigured day length freezes instead of NaN/inf") {
	CHECK(advance_time_of_day(500.0, 1.0, 0.0) == doctest::Approx(500.0));
	CHECK(advance_time_of_day(500.0, 1.0, -5.0) == doctest::Approx(500.0));
}

TEST_CASE("sky_brightness: peaks at noon, dims toward midnight") {
	const double noon = sky_brightness(kTicksPerDay / 4);
	const double midnight = sky_brightness((kTicksPerDay * 3) / 4);
	const double sunrise = sky_brightness(0);
	CHECK(noon > sunrise);
	CHECK(noon > midnight);
	CHECK(midnight < sunrise);
	CHECK(noon == doctest::Approx(1.0));
}

TEST_CASE("sky_brightness/sky_color_for_time: wraps cleanly past kTicksPerDay") {
	CHECK(sky_brightness(kTicksPerDay) == doctest::Approx(sky_brightness(0)));
	const SkyColor a = sky_color_for_time(kTicksPerDay);
	const SkyColor b = sky_color_for_time(0);
	CHECK(a.r == b.r);
	CHECK(a.g == b.g);
	CHECK(a.b == b.b);
}

TEST_CASE("sky_color_for_time: noon is brighter/bluer than midnight") {
	const SkyColor noon = sky_color_for_time(kTicksPerDay / 4);
	const SkyColor midnight = sky_color_for_time((kTicksPerDay * 3) / 4);
	CHECK(noon.b > midnight.b);
	// Midnight should be dark overall (low channel values).
	CHECK(midnight.r < 50);
	CHECK(midnight.g < 50);
	CHECK(midnight.b < 50);
}

TEST_CASE("sky_color_for_time: interpolates smoothly between keyframes") {
	const SkyColor at_stop = sky_color_for_time(0);
	const SkyColor just_after = sky_color_for_time(1);
	// One tick of drift out of a 6000-tick segment should barely move.
	CHECK(std::abs(static_cast<int>(at_stop.r) - static_cast<int>(just_after.r)) <= 1);
}
