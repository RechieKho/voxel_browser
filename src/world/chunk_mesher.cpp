#include "vb/world/chunk_mesher.hpp"

#include "vb/world/chunk_mesh_snapshot.hpp"

// mesh_chunk() is a thin synchronous convenience wrapper: build a snapshot
// from `store` (main-thread only) and mesh it in place. The actual meshing
// logic lives in chunk_mesh_snapshot.cpp as a pure function of the snapshot,
// so it can also run on a background thread -- see ChunkMeshWorkerPool.

namespace vb::world {

MeshData mesh_chunk(const ClientChunkStore &store, core::ChunkCoord coord) {
	const ChunkMeshSnapshot snapshot = build_chunk_mesh_snapshot(store, coord);
	return mesh_chunk_from_snapshot(snapshot, store.registry());
}

} // namespace vb::world
