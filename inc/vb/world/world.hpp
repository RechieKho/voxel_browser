#pragma once

#include <memory>
#include <unordered_map>
#include <vector>

#include "vb/core/ids.hpp"
#include "vb/core/math.hpp"
#include "vb/world/block.hpp"
#include "vb/world/block_query.hpp"
#include "vb/world/chunk.hpp"

// Server-side voxel world: a ChunkCoord -> Chunk map plus block accessors in
// world-voxel coordinates (spec §5.3). The load/unload manager is driven by the
// ChunkLifecycleSystem (§2.4); this class only owns storage + queries.

namespace vb::world {

// world voxel -> {chunk coord, local 0..31 coord}
struct VoxelAddress {
	core::ChunkCoord chunk;
	int lx = 0;
	int ly = 0;
	int lz = 0;
};

VoxelAddress address_of(core::IVec3 world_voxel);

class World : public BlockSolidQuery {
public:
	explicit World(BlockRegistry registry) : registry_(std::move(registry)) {}

	const BlockRegistry &registry() const { return registry_; }

	bool has_chunk(core::ChunkCoord c) const {
		return chunks_.find(c) != chunks_.end();
	}
	Chunk *find_chunk(core::ChunkCoord c);
	const Chunk *find_chunk(core::ChunkCoord c) const;
	Chunk &get_or_create_chunk(core::ChunkCoord c);
	// Take ownership of an externally generated chunk (worldgen worker output).
	// Replaces any existing chunk at that coord.
	Chunk &insert_chunk(std::unique_ptr<Chunk> chunk);
	bool unload_chunk(core::ChunkCoord c);
	std::size_t chunk_count() const { return chunks_.size(); }

	std::vector<core::ChunkCoord> loaded_coords() const;

	// Block accessors in world-voxel coordinates. get returns kAir for
	// unloaded chunks; set creates the chunk if needed and returns whether the
	// voxel changed.
	core::BlockId get_block(core::IVec3 world_voxel) const;
	bool set_block(core::IVec3 world_voxel, core::BlockId block);

	// BlockSolidQuery
	core::BlockId block_at(core::IVec3 world_voxel) const override {
		return get_block(world_voxel);
	}
	bool solid_at(core::IVec3 world_voxel) const override {
		return registry_.is_solid(get_block(world_voxel));
	}

private:
	BlockRegistry registry_;
	std::unordered_map<core::ChunkCoord, std::unique_ptr<Chunk>> chunks_;
};

} // namespace vb::world
