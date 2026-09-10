#pragma once

#include "vb/core/math.hpp"
#include "vb/world/block_query.hpp"

// Shared player movement + voxel collision (spec §7.3 / §12). One implementation,
// deterministic given identical inputs, consumed by both the server
// (VoxelCollisionSystem) and client-side prediction. No raylib, no ECS — pure
// math against a BlockSolidQuery, so it is unit-tested headless.

namespace vb::physics {

// Player collision box + movement tunables. Engine defaults; a Lua pack overrides
// per entity kind in Phase 4.
struct MoveParams {
	double half_width = 0.4; // x/z half-extent of the AABB
	double height = 1.8; // full height, feet -> top
	double eye_height = 1.62;
	double walk_speed = 4.5; // m/s
	double sprint_speed = 7.0;
	double accel = 45.0; // horizontal accel toward wish velocity (m/s^2)
	double air_accel = 10.0;
	double friction = 12.0; // ground friction (1/s)
	double gravity = 28.0; // m/s^2, downward
	double jump_speed = 8.9;
	double terminal_velocity = 60.0;
	double step_height = 1.05; // climb a full voxel step without jumping
	double fly_speed = 12.0;
	bool fly = false;
};

struct MoveState {
	core::Vec3d position{}; // feet position (AABB center in x/z, min in y)
	core::Vec3d velocity{};
	bool on_ground = false;

	bool operator==(const MoveState &) const = default;
};

// One player's intent for a single simulation step. `wish_dir` is world-space,
// xz only, already rotated by yaw and clamped to length <= 1.
struct MoveInput {
	core::Vec3d wish_dir{};
	bool jump = false;
	bool sprint = false;
	bool fly_up = false; // fly mode only
	bool fly_down = false;
	double dt = 0.0;
};

// Deterministic authoritative + predicted movement step. Unloaded/unknown voxels
// are treated as air.
MoveState step_movement(const MoveState &state, const MoveInput &input,
		const MoveParams &params, const world::BlockSolidQuery &world);

// Rotate a local move vector (x = strafe right, z = forward) by `yaw_degrees`
// into world space and clamp to unit length. Matches render::FirstPersonController:
// forward is -Z at yaw 0.
core::Vec3d wish_dir_from_local(core::Vec3f local_move, float yaw_degrees);

// The player AABB with its feet at `feet`.
core::AABB player_box(core::Vec3d feet, const MoveParams &params);

} // namespace vb::physics
