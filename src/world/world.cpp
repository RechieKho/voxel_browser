#include "vb/world/world.hpp"

#include "vb/core/math.hpp"

namespace vb::world {

VoxelAddress address_of(core::IVec3 world_voxel) {
	VoxelAddress a;
	a.chunk = { core::floor_div(world_voxel.x, kChunkDim),
		core::floor_div(world_voxel.y, kChunkDim),
		core::floor_div(world_voxel.z, kChunkDim) };
	a.lx = core::floor_mod(world_voxel.x, kChunkDim);
	a.ly = core::floor_mod(world_voxel.y, kChunkDim);
	a.lz = core::floor_mod(world_voxel.z, kChunkDim);
	return a;
}

Chunk *World::find_chunk(core::ChunkCoord c) {
	const auto it = chunks_.find(c);
	return it == chunks_.end() ? nullptr : it->second.get();
}

const Chunk *World::find_chunk(core::ChunkCoord c) const {
	const auto it = chunks_.find(c);
	return it == chunks_.end() ? nullptr : it->second.get();
}

Chunk &World::get_or_create_chunk(core::ChunkCoord c) {
	auto it = chunks_.find(c);
	if (it == chunks_.end()) {
		it = chunks_.emplace(c, std::make_unique<Chunk>(c)).first;
	}
	return *it->second;
}

bool World::unload_chunk(core::ChunkCoord c) { return chunks_.erase(c) != 0; }

std::vector<core::ChunkCoord> World::loaded_coords() const {
	std::vector<core::ChunkCoord> out;
	out.reserve(chunks_.size());
	for (const auto &[coord, chunk] : chunks_) {
		(void)chunk;
		out.push_back(coord);
	}
	return out;
}

core::BlockId World::get_block(core::IVec3 world_voxel) const {
	const VoxelAddress a = address_of(world_voxel);
	const Chunk *chunk = find_chunk(a.chunk);
	return chunk == nullptr ? core::BlockId::kAir : chunk->get(a.lx, a.ly, a.lz);
}

bool World::set_block(core::IVec3 world_voxel, core::BlockId block) {
	const VoxelAddress a = address_of(world_voxel);
	Chunk &chunk = get_or_create_chunk(a.chunk);
	return chunk.set(a.lx, a.ly, a.lz, block);
}

} // namespace vb::world
