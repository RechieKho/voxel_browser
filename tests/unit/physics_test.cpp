#include <doctest/doctest.h>

#include <ostream>

#include "vb/physics/movement.hpp"
#include "vb/world/block_query.hpp"

using namespace vb;
using vb::core::IVec3;
using vb::core::Vec3d;
using vb::core::Vec3f;

namespace {

// Solid everything at y < floor_y; optionally a wall of solid columns.
struct FlatWorld final : world::BlockSolidQuery {
	int floor_y = 0;
	int wall_x = 1000000; // x >= wall_x is solid (for y in [floor_y, floor_y+4))

	core::BlockId block_at(IVec3 v) const override {
		return solid_at(v) ? core::BlockId{ 1 } : core::BlockId::kAir;
	}
	bool solid_at(IVec3 v) const override {
		if (v.y < floor_y) {
			return true;
		}
		if (v.x >= wall_x && v.y < floor_y + 4) {
			return true;
		}
		return false;
	}
};

physics::MoveInput walk(Vec3d dir, double dt, bool jump = false) {
	physics::MoveInput in;
	in.wish_dir = dir;
	in.dt = dt;
	in.jump = jump;
	return in;
}

physics::MoveState settle(physics::MoveState s, const physics::MoveParams &p,
		const world::BlockSolidQuery &w, int ticks) {
	for (int i = 0; i < ticks; ++i) {
		s = physics::step_movement(s, walk({}, 0.05), p, w);
	}
	return s;
}

} // namespace

TEST_CASE("gravity: a player falls and comes to rest on the floor") {
	FlatWorld world;
	world.floor_y = 64;
	physics::MoveParams p;
	physics::MoveState s;
	s.position = { 0.0, 70.0, 0.0 };

	s = settle(s, p, world, 120);

	CHECK(s.position.y == doctest::Approx(64.0).epsilon(0.02));
	CHECK(s.on_ground);
	CHECK(s.velocity.y == doctest::Approx(0.0).epsilon(0.001));
}

TEST_CASE("a player does not sink through the floor at high speed") {
	FlatWorld world;
	world.floor_y = 0;
	physics::MoveParams p;
	physics::MoveState s;
	s.position = { 0.0, 40.0, 0.0 };
	s.velocity = { 0.0, -55.0, 0.0 };

	s = settle(s, p, world, 200);
	CHECK(s.position.y >= -0.001);
	CHECK(s.position.y == doctest::Approx(0.0).epsilon(0.02));
}

TEST_CASE("walking into a wall stops horizontal motion but not sliding") {
	FlatWorld world;
	world.floor_y = 0;
	world.wall_x = 3;
	physics::MoveParams p;
	physics::MoveState s;
	s.position = { 0.0, 0.0, 0.0 };
	s.on_ground = true;

	for (int i = 0; i < 80; ++i) {
		s = physics::step_movement(s, walk({ 1.0, 0.0, 0.0 }, 0.05), p, world);
	}
	// Box half-width 0.4, wall at x=3 -> player centre can't pass ~2.6.
	CHECK(s.position.x <= 2.61);
	CHECK(s.position.x > 1.5);

	// Now also push along +z: should slide freely along the wall.
	const double x_before = s.position.x;
	for (int i = 0; i < 40; ++i) {
		s = physics::step_movement(
				s, walk({ 1.0, 0.0, 1.0 }, 0.05), p, world);
	}
	CHECK(s.position.z > 3.0);
	CHECK(s.position.x == doctest::Approx(x_before).epsilon(0.05));
}

TEST_CASE("jump leaves the ground and returns to it") {
	FlatWorld world;
	world.floor_y = 0;
	physics::MoveParams p;
	physics::MoveState s;
	s.position = { 0.0, 0.0, 0.0 };
	s = settle(s, p, world, 10);
	REQUIRE(s.on_ground);

	s = physics::step_movement(s, walk({}, 0.05, /*jump*/ true), p, world);
	CHECK_FALSE(s.on_ground);
	CHECK(s.velocity.y > 0.0);

	s = settle(s, p, world, 120);
	CHECK(s.on_ground);
	CHECK(s.position.y == doctest::Approx(0.0).epsilon(0.02));
}

TEST_CASE("step-up: walk onto a one-block ledge without jumping") {
	FlatWorld world;
	world.floor_y = 0;
	world.wall_x = 1000000;
	// A single solid block column at x in [2,3), y in [0,1) acting as a step.
	struct Stepped final : world::BlockSolidQuery {
		core::BlockId block_at(IVec3 v) const override {
			return solid_at(v) ? core::BlockId{ 1 } : core::BlockId::kAir;
		}
		bool solid_at(IVec3 v) const override {
			if (v.y < 0) {
				return true;
			}
			return v.y == 0 && v.x >= 2; // a raised platform one block high
		}
	} stepped;

	physics::MoveParams p;
	physics::MoveState s;
	s.position = { 0.0, 0.0, 0.0 };
	s.on_ground = true;
	for (int i = 0; i < 120; ++i) {
		s = physics::step_movement(
				s, walk({ 1.0, 0.0, 0.0 }, 0.05), p, stepped);
	}
	CHECK(s.position.x > 3.0); // climbed onto the platform and kept going
	CHECK(s.position.y == doctest::Approx(1.0).epsilon(0.1));
}

TEST_CASE("wish_dir_from_local respects yaw (forward is -Z at yaw 0)") {
	const Vec3d fwd = physics::wish_dir_from_local(Vec3f{ 0.0f, 0.0f, 1.0f }, 0.0f);
	CHECK(fwd.x == doctest::Approx(0.0));
	CHECK(fwd.z == doctest::Approx(-1.0));

	const Vec3d right =
			physics::wish_dir_from_local(Vec3f{ 1.0f, 0.0f, 0.0f }, 0.0f);
	CHECK(right.x == doctest::Approx(1.0));
	CHECK(right.z == doctest::Approx(0.0));

	// Yaw 90 deg: forward should point +X.
	const Vec3d y90 = physics::wish_dir_from_local(Vec3f{ 0.0f, 0.0f, 1.0f }, 90.0f);
	CHECK(y90.x == doctest::Approx(1.0));
	CHECK(y90.z == doctest::Approx(0.0).epsilon(1e-6));
}
