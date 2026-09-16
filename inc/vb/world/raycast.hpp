#pragma once

#include "vb/core/math.hpp"
#include "vb/world/block_query.hpp" // BlockSolidQuery

// Shared voxel-grid raycast (Amanatides & Woo traversal). Used by the client's
// block-selection highlight and by Phase 4.2's `vb.world.raycast`.

namespace vb::world {

struct VoxelRayHit {
	bool hit = false;
	core::IVec3 voxel{};
	core::IVec3 normal{};
};

// Walks the voxel grid from `origin` along unit `dir`, stopping at the first
// solid voxel or after `max_dist` world units.
VoxelRayHit raycast_voxel(const BlockSolidQuery &world, core::Vec3d origin,
		core::Vec3d dir, double max_dist);

} // namespace vb::world
