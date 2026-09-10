#pragma once

#include "vb/core/ids.hpp"
#include "vb/core/math.hpp"

// Read-only voxel solidity query shared by physics and meshing (spec §12). Both
// the server world and the client chunk mirror implement it, so the swept-AABB
// collision routine in vb_core is written once against this interface.

namespace vb::world {

class BlockSolidQuery {
public:
	virtual ~BlockSolidQuery() = default;

	// Block at a world-space integer voxel coordinate. Out-of-loaded-range
	// returns kAir (callers treat unknown as non-solid / open air).
	virtual core::BlockId block_at(core::IVec3 world_voxel) const = 0;

	// Convenience: whether that voxel participates in collision.
	virtual bool solid_at(core::IVec3 world_voxel) const = 0;
};

} // namespace vb::world
