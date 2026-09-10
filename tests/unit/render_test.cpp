#include <doctest/doctest.h>

#include <cmath>

#include "vb/render/camera.hpp"

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
