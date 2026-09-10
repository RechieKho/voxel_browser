#pragma once

#include <memory>
#include <unordered_map>
#include <vector>

#include "vb/core/error.hpp"
#include "vb/core/ids.hpp"
#include "vb/core/result.hpp"
#include "vb/protocol/world.hpp"
#include "vb/world/block.hpp"
#include "vb/world/block_query.hpp"
#include "vb/world/chunk.hpp"

// Client-side mirror of the replicated chunk data (spec §11.2). Applies
// S2C_ChunkAdd / Delta / Remove; feeds meshing and client-side physics
// prediction via BlockSolidQuery.

namespace vb::world {

class ClientChunkStore : public BlockSolidQuery {
public:
	explicit ClientChunkStore(BlockRegistry registry) : registry_(std::move(registry)) {}

	core::Result<void, core::ProtocolError> apply_add(
			const protocol::S2CChunkAdd &msg);
	core::Result<void, core::ProtocolError> apply_delta(
			const protocol::S2CChunkDelta &msg);
	void apply_remove(const protocol::S2CChunkRemove &msg);

	const Chunk *find(core::ChunkCoord c) const;
	bool has(core::ChunkCoord c) const { return chunks_.count(c) != 0; }
	std::size_t size() const { return chunks_.size(); }
	std::vector<core::ChunkCoord> loaded_coords() const;

	const BlockRegistry &registry() const { return registry_; }

	// BlockSolidQuery
	core::BlockId block_at(core::IVec3 world_voxel) const override;
	bool solid_at(core::IVec3 world_voxel) const override {
		return registry_.is_solid(block_at(world_voxel));
	}

	// Light at a world voxel; a full-bright Light for unloaded chunks so the
	// mesher doesn't darken chunk borders while neighbours stream in.
	Light light_at(core::IVec3 world_voxel) const;

private:
	BlockRegistry registry_;
	std::unordered_map<core::ChunkCoord, std::unique_ptr<Chunk>> chunks_;
};

} // namespace vb::world
