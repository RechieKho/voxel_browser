#pragma once

#include <cmath>

#include "vb/core/ids.hpp"
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

// True if the chunk column a player is standing in is loaded deeply enough
// that falling wouldn't tunnel into unloaded (air-by-default) space before
// hitting real ground: the player's own chunk plus two chunks below it (a
// generous margin -- spawn is normally within one chunk of the surface).
//
// Guards the join-time race between "world replication requested the spawn
// chunk" and "the async worldgen worker actually finished it": without this,
// a freshly-joined player free-falls with zero collision (unloaded chunks
// have no blocks) until the chunk arrives, by which point their position may
// already be below the real surface -- collision only ever prevents *new*
// penetration during a move, it never resolves a pre-existing one, so they'd
// end up permanently embedded in terrain. Callers should skip step_movement
// entirely (leave the player's position/velocity frozen) while this is false,
// rather than call it with a zero/degenerate input -- freezing avoids not
// just falling but also any horizontal drift while the world is still empty.
//
// `has_chunk` is anything callable as `bool(core::ChunkCoord)` -- lets this
// work against both the server's World and the client's ClientChunkStore
// without a shared base interface for chunk presence.
template <typename HasChunkFn>
bool ground_area_loaded(core::Vec3d feet, HasChunkFn &&has_chunk) {
	const core::IVec3 voxel{ static_cast<std::int32_t>(std::floor(feet.x)),
		static_cast<std::int32_t>(std::floor(feet.y)),
		static_cast<std::int32_t>(std::floor(feet.z)) };
	const core::ChunkCoord base = core::chunk_of(voxel);
	for (std::int32_t dy = 0; dy >= -2; --dy) {
		if (!has_chunk(core::ChunkCoord{ base.x, base.y + dy, base.z })) {
			return false;
		}
	}
	return true;
}

} // namespace vb::physics
