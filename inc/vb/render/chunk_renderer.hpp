#pragma once

#include <cstddef>
#include <cstdint>
#include <unordered_map>

#include "vb/core/ids.hpp"
#include "vb/world/chunk_mesh_worker_pool.hpp"
#include "vb/world/client_chunk_store.hpp"

// Owns the GPU-side chunk meshes for the client (spec §11.2). Each frame,
// sync() submits changed chunks to a background ChunkMeshWorkerPool (budgeted)
// and GPU-uploads whatever meshing has finished since the last call; draw()
// renders the lot. Meshing itself (CPU face-culling + AO) never runs on the
// main thread past the cheap snapshot copy -- only the GPU upload does, which
// raylib requires.

namespace vb::render {

class ChunkRenderer {
public:
	// mesh_threads is forwarded to the internal ChunkMeshWorkerPool (0 =
	// default thread count, world::ChunkMeshWorkerPool::kSynchronous for
	// deterministic single-threaded behaviour).
	explicit ChunkRenderer(std::size_t mesh_threads = 0);
	~ChunkRenderer();

	ChunkRenderer(const ChunkRenderer &) = delete;
	ChunkRenderer &operator=(const ChunkRenderer &) = delete;

	// Submit at most `submit_budget` changed/new chunks to the background mesh
	// pool, then drain and GPU-upload whatever mesh jobs finished since the
	// last call. A chunk's mesh can now lag its data arriving by a frame or
	// more -- see chunk_mesh_worker_pool.hpp.
	void sync(const world::ClientChunkStore &store, int submit_budget = 8);

	// Draw every uploaded chunk. Call inside BeginMode3D/EndMode3D.
	void draw() const;

	std::size_t uploaded_count() const { return gpu_.size(); }
	std::size_t pending_mesh_count() const { return pool_.pending(); }

private:
	struct GpuChunk;
	void upload(core::ChunkCoord coord, const world::MeshData &data, std::uint64_t revision);
	void drop(core::ChunkCoord coord);

	world::ChunkMeshWorkerPool pool_;
	std::unordered_map<core::ChunkCoord, GpuChunk> gpu_;
};

} // namespace vb::render
