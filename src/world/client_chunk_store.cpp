#include "vb/world/client_chunk_store.hpp"

#include "vb/world/chunk_codec.hpp"
#include "vb/world/lighting.hpp"
#include "vb/world/paletted_chunk_store.hpp"
#include "vb/world/world.hpp"

namespace vb::world {

void ClientChunkStore::bump_all_neighbor_revisions(core::ChunkCoord coord) {
	// The full 26-neighbourhood, not just the 6 face-adjacent chunks: AO
	// samples the 3 voxels diagonally around each face corner (see
	// chunk_mesher.cpp's `a`/`bpt`/`d`), and for a voxel sitting on a chunk
	// edge or corner those samples land in an edge- or corner-adjacent chunk,
	// not a face neighbour. Only bumping the 6 face neighbours left AO at
	// chunk edges/corners permanently stale whenever the chunk that actually
	// supplied that diagonal sample arrived/changed/left later.
	for (int dz = -1; dz <= 1; ++dz) {
		for (int dy = -1; dy <= 1; ++dy) {
			for (int dx = -1; dx <= 1; ++dx) {
				if (dx == 0 && dy == 0 && dz == 0) {
					continue;
				}
				const auto it = chunks_.find(
						{ coord.x + dx, coord.y + dy, coord.z + dz });
				if (it != chunks_.end()) {
					it->second->bump_revision();
				}
			}
		}
	}
}

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
	// Neighbours already loaded were meshed assuming this chunk was unloaded
	// (open air) -- their border faces/AO need recomputing now that it's not.
	bump_all_neighbor_revisions(msg.coord);
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
	// An authoritative edit (this player's or another's) may have touched a
	// border voxel -- a neighbour's culling/AO could depend on it. Delta lists
	// can span anywhere in the chunk, so bump unconditionally rather than
	// working out which specific voxels are actually on an edge.
	bump_all_neighbor_revisions(msg.coord);
	return {};
}

void ClientChunkStore::apply_remove(const protocol::S2CChunkRemove &msg) {
	chunks_.erase(msg.coord);
	// A neighbour may have been culling faces against this chunk; now that
	// it's gone those faces need to reappear.
	bump_all_neighbor_revisions(msg.coord);
}

core::BlockId ClientChunkStore::edit_block(core::IVec3 world_voxel,
		core::BlockId block) {
	const VoxelAddress a = address_of(world_voxel);
	const auto it = chunks_.find(a.chunk);
	if (it == chunks_.end()) {
		return core::BlockId::kAir;
	}
	Chunk &chunk = *it->second;
	const core::BlockId prev = chunk.get(a.lx, a.ly, a.lz);
	if (chunk.set(a.lx, a.ly, a.lz, block)) {
		// Predicted edits only ever touched the block, never the light volume,
		// which used to leave whatever light value was there before the edit
		// (e.g. 0 for a block that was buried underground) on the voxel that
		// just became open air -- mesh_chunk reads that stale value for any
		// newly-exposed neighbouring face, so breaking a block flashed a
		// near-black face for the round trip until the server's authoritative
		// S2C_ChunkDelta (which does carry a relit value) arrived. Relighting
		// here immediately, with the exact same per-chunk algorithm the
		// server runs on the same edit, makes the prediction already match
		// what that delta will say in the common case (no concurrent edit to
		// this chunk), instead of just shortening the stale window.
		//
		// relight_column (not a plain relight_chunk call) so a predicted edit
		// at the top of this chunk also cascades correctly into an
		// already-loaded chunk below, instead of just fixing this one chunk in
		// isolation -- see lighting.hpp.
		const LightEngine light_engine(registry_);
		relight_column(light_engine, a.chunk,
				[&](core::ChunkCoord c) { return find(c); },
				[](core::ChunkCoord, const std::array<Light, kChunkVolume> &,
						const Chunk &) {});
		// A border edit changes the neighbour's culled faces too.
		auto bump_neighbour = [&](int dx, int dy, int dz) {
			const auto n = chunks_.find({ a.chunk.x + dx, a.chunk.y + dy,
					a.chunk.z + dz });
			if (n != chunks_.end()) {
				n->second->bump_revision();
			}
		};
		if (a.lx == 0) {
			bump_neighbour(-1, 0, 0);
		}
		if (a.lx == kChunkDim - 1) {
			bump_neighbour(1, 0, 0);
		}
		if (a.ly == 0) {
			bump_neighbour(0, -1, 0);
		}
		if (a.ly == kChunkDim - 1) {
			bump_neighbour(0, 1, 0);
		}
		if (a.lz == 0) {
			bump_neighbour(0, 0, -1);
		}
		if (a.lz == kChunkDim - 1) {
			bump_neighbour(0, 0, 1);
		}
	}
	return prev;
}

Chunk *ClientChunkStore::find(core::ChunkCoord c) {
	const auto it = chunks_.find(c);
	return it == chunks_.end() ? nullptr : it->second.get();
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
