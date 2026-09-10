#include "vb/world/client_chunk_store.hpp"

#include "vb/world/chunk_codec.hpp"
#include "vb/world/paletted_chunk_store.hpp"
#include "vb/world/world.hpp"

namespace vb::world {

core::Result<void, core::ProtocolError> ClientChunkStore::apply_add(
		const protocol::S2CChunkAdd &msg) {
	auto chunk = std::make_unique<Chunk>(msg.coord);
	auto decoded =
			decode_chunk_payload({ msg.payload.data(), msg.payload.size() }, *chunk);
	if (!decoded) {
		return core::Err{ decoded.error() };
	}
	chunk->set_gen_state(GenState::kGenerated);
	chunk->set_revision(msg.revision);
	chunk->dirty().mesh = true;
	chunks_.insert_or_assign(msg.coord, std::move(chunk));
	return {};
}

core::Result<void, core::ProtocolError> ClientChunkStore::apply_delta(
		const protocol::S2CChunkDelta &msg) {
	const auto it = chunks_.find(msg.coord);
	if (it == chunks_.end()) {
		return core::Err{ core::ProtocolError::kMalformed }; // delta before add
	}
	Chunk &chunk = *it->second;
	for (const protocol::BlockChange &c : msg.blocks) {
		if (c.local_index >= kChunkVolume) {
			return core::Err{ core::ProtocolError::kMalformed };
		}
		chunk.blocks().set(c.local_index, c.block);
	}
	for (const protocol::LightChange &c : msg.light) {
		if (c.local_index >= kChunkVolume) {
			return core::Err{ core::ProtocolError::kMalformed };
		}
		chunk.light_volume()[c.local_index].packed = c.packed;
	}
	chunk.dirty().mesh = true;
	chunk.bump_revision();
	return {};
}

void ClientChunkStore::apply_remove(const protocol::S2CChunkRemove &msg) {
	chunks_.erase(msg.coord);
}

const Chunk *ClientChunkStore::find(core::ChunkCoord c) const {
	const auto it = chunks_.find(c);
	return it == chunks_.end() ? nullptr : it->second.get();
}

std::vector<core::ChunkCoord> ClientChunkStore::loaded_coords() const {
	std::vector<core::ChunkCoord> out;
	out.reserve(chunks_.size());
	for (const auto &[coord, chunk] : chunks_) {
		(void)chunk;
		out.push_back(coord);
	}
	return out;
}

core::BlockId ClientChunkStore::block_at(core::IVec3 world_voxel) const {
	const VoxelAddress a = address_of(world_voxel);
	const Chunk *chunk = find(a.chunk);
	return chunk == nullptr ? core::BlockId::kAir : chunk->get(a.lx, a.ly, a.lz);
}

Light ClientChunkStore::light_at(core::IVec3 world_voxel) const {
	const VoxelAddress a = address_of(world_voxel);
	const Chunk *chunk = find(a.chunk);
	if (chunk == nullptr) {
		Light full;
		full.set_sky(15);
		return full;
	}
	return chunk->light(a.lx, a.ly, a.lz);
}

} // namespace vb::world
