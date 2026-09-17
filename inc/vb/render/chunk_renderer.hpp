#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <unordered_map>
#include <unordered_set>

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
	// pool, then GPU-upload at most `upload_budget` finished mesh jobs (a
	// first-time upload does a real UnloadModel/UploadMesh -- VAO/VBO churn --
	// so an unbounded drain-and-upload-all-at-once burst on join is exactly
	// the trigger for the NVIDIA driver heap corruption documented in
	// STATE.md; any jobs finished but not yet uploaded stay queued and are
	// uploaded on a later call). A chunk's mesh can now lag its data arriving
	// by a frame or more -- see chunk_mesh_worker_pool.hpp.
	void sync(const world::ClientChunkStore &store, int submit_budget = 8, int upload_budget = 4);

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
	std::deque<world::ChunkMeshResult> pending_uploads_;
	// Mirrors the coords in pending_uploads_ for O(1) "already waiting to
	// upload, don't resubmit" checks -- pool_.in_flight_or_queued() alone
	// can't see this queue, since a result leaves the pool the moment it's
	// polled into pending_uploads_ but may sit there for several frames
	// under upload_budget pacing.
	std::unordered_set<core::ChunkCoord> pending_coords_;
};

} // namespace vb::render
