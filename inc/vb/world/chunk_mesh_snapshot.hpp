#pragma once

#include <cstdint>
#include <vector>

#include "vb/core/ids.hpp"
#include "vb/world/block.hpp"
#include "vb/world/chunk.hpp"
#include "vb/world/chunk_mesher.hpp"
#include "vb/world/client_chunk_store.hpp"

// A thread-safe, self-contained copy of everything mesh_chunk() needs for one
// chunk: its own kChunkDim^3 voxels/light plus a 1-voxel border shell pulled
// from its neighbours (mesh_chunk's AO sampling never reaches further than
// that -- every corner sample offsets by at most one voxel along each axis
// from the target block's own face, see the .cpp).
//
// Building a snapshot still has to happen on whichever thread owns
// ClientChunkStore (apply_add/delta/remove and this are not safe to run
// concurrently), but once built it holds no reference into the store, so the
// actual meshing (mesh_chunk_from_snapshot) can safely run on a background
// thread -- see ChunkMeshWorkerPool.

namespace vb::world {

inline constexpr int kMeshSnapshotDim = kChunkDim + 2; // 1-voxel padding each side
inline constexpr std::size_t kMeshSnapshotVolume =
		static_cast<std::size_t>(kMeshSnapshotDim) * kMeshSnapshotDim * kMeshSnapshotDim;

struct ChunkMeshSnapshot {
	core::ChunkCoord coord{};
	std::uint64_t revision = 0;
	bool loaded = false; // false if the chunk itself wasn't loaded at snapshot time
	std::vector<core::BlockId> blocks; // kMeshSnapshotVolume entries, see padded_index()
	std::vector<Light> lights; // kMeshSnapshotVolume entries
};

// (px, py, pz) are chunk-local coordinates in [-1, kChunkDim] (1-voxel padding
// on both sides of the real [0, kChunkDim) block range).
constexpr std::size_t padded_index(int px, int py, int pz) {
	return static_cast<std::size_t>(px + 1) +
			static_cast<std::size_t>(kMeshSnapshotDim) *
					(static_cast<std::size_t>(pz + 1) +
							static_cast<std::size_t>(kMeshSnapshotDim) *
									static_cast<std::size_t>(py + 1));
}

// Copies `coord`'s voxels/light plus a 1-voxel border from `store` (must run
// on the thread that owns `store`). `loaded` is false (blocks/lights left
// empty) if `coord` itself has no chunk yet.
ChunkMeshSnapshot build_chunk_mesh_snapshot(const ClientChunkStore &store, core::ChunkCoord coord);

// Pure function over the snapshot -- no store access, safe on any thread.
MeshData mesh_chunk_from_snapshot(const ChunkMeshSnapshot &snapshot, const BlockRegistry &registry);

} // namespace vb::world
