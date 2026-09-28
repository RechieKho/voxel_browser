#include <doctest/doctest.h>

#include <cmath>

#include "vb/render/camera.hpp"

using vb::render::EyeHeightSmoother;
using vb::render::FirstPersonController;
using vb::render::LookMoveInput;

TEST_CASE("controller looks along -Z at zero yaw/pitch") {
	FirstPersonController c;
	c.set_position({ 0.0, 0.0, 0.0 });
	const auto f = c.forward();
	CHECK(f.x == doctest::Approx(0.0).epsilon(0.001));
	CHECK(f.y == doctest::Approx(0.0).epsilon(0.001));
	CHECK(f.z == doctest::Approx(-1.0).epsilon(0.001));
}

TEST_CASE("mouse look accumulates yaw and clamps pitch") {
	FirstPersonController c;
	c.set_sensitivity(0.1);

	LookMoveInput in;
	in.look_delta = { 100.0, 0.0 };
	c.update(in, 0.0);
	CHECK(c.yaw() == doctest::Approx(10.0));

	in.look_delta = { 0.0, 100000.0 }; // slam the mouse up
	c.update(in, 0.0);
	CHECK(c.pitch() >= -89.0);
	CHECK(c.pitch() <= 89.0);
}

TEST_CASE("forward movement follows yaw, ignores pitch magnitude") {
	FirstPersonController c;
	c.set_position({ 0.0, 10.0, 0.0 });
	c.set_look(0.0, -45.0); // looking down
	c.set_speed(10.0);

	LookMoveInput in;
	in.move_axis.z = 1.0; // forward
	c.update(in, 1.0);

	// 10 u/s for 1 s straight along -Z; pitch must not bleed into horizontal
	// speed and must not change altitude.
	CHECK(c.position().z == doctest::Approx(-10.0));
	CHECK(c.position().y == doctest::Approx(10.0));
	CHECK(c.position().x == doctest::Approx(0.0));
}

TEST_CASE("diagonal input is normalized") {
	FirstPersonController c;
	c.set_position({ 0.0, 0.0, 0.0 });
	c.set_speed(10.0);

	LookMoveInput in;
	in.move_axis.x = 1.0;
	in.move_axis.z = 1.0;
	c.update(in, 1.0);

	const double dist = std::sqrt(c.position().x * c.position().x +
			c.position().z * c.position().z);
	CHECK(dist == doctest::Approx(10.0)); // not 10*sqrt(2)
}

TEST_CASE("sprint multiplies speed") {
	FirstPersonController c;
	c.set_speed(10.0);
	c.set_sprint_multiplier(2.0);

	LookMoveInput in;
	in.move_axis.z = 1.0;
	in.sprint = true;
	c.update(in, 1.0);
	CHECK(c.position().z == doctest::Approx(-20.0));
}

TEST_CASE("eye height smoother's first update snaps to the target") {
	EyeHeightSmoother s;
	CHECK(s.update(64.0, 1.0 / 60.0) == doctest::Approx(64.0));
}

TEST_CASE("eye height smoother eases a step-up instead of popping instantly") {
	EyeHeightSmoother s;
	s.reset(64.0);
	// A step-up moves feet.y by a full block in one tick -- the very next
	// frame should be partway there, not already at 65.0.
	const double after_one_frame = s.update(65.0, 1.0 / 60.0);
	CHECK(after_one_frame > 64.0);
	CHECK(after_one_frame < 65.0);
}

TEST_CASE("eye height smoother converges to the target over time") {
	EyeHeightSmoother s;
	s.reset(64.0);
	double y = 64.0;
	for (int i = 0; i < 600; ++i) { // 10s at 60Hz, far past the time constant
		y = s.update(65.0, 1.0 / 60.0);
	}
	CHECK(y == doctest::Approx(65.0).epsilon(0.0001));
}

TEST_CASE("eye height smoother snaps immediately on a large jump (teleport)") {
	EyeHeightSmoother s;
	s.reset(64.0);
	CHECK(s.update(500.0, 1.0 / 60.0) == doctest::Approx(500.0));
}

TEST_CASE("eye height smoother tracks a small continuous change almost exactly") {
	EyeHeightSmoother s;
	s.reset(64.0);
	// Normal walking/falling moves feet.y by a tiny amount per frame -- the
	// smoothing lag on a delta this small should be negligible.
	const double y = s.update(64.01, 1.0 / 60.0);
	CHECK(y == doctest::Approx(64.01).epsilon(0.01));
}

TEST_CASE("eye height smoother reset drops any in-flight smoothing") {
	EyeHeightSmoother s;
	s.reset(64.0);
	s.update(65.0, 1.0 / 600.0); // barely moved toward 65.0 yet
	s.reset(10.0);
	CHECK(s.update(10.0, 1.0 / 60.0) == doctest::Approx(10.0));
}
