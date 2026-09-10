#pragma once

#include <cstddef>
#include <cstdint>
#include <unordered_map>

#include "vb/core/ids.hpp"
#include "vb/world/client_chunk_store.hpp"

// Owns the GPU-side chunk meshes for the client (spec §11.2). Each frame,
// sync() re-meshes changed chunks (main-thread, budgeted) and uploads them,
// drops meshes for unloaded chunks, and draw() renders the lot. Meshing on a
// worker pool is a follow-up (needs a chunk+neighbour snapshot).

namespace vb::render {

class ChunkRenderer {
public:
	ChunkRenderer();
	~ChunkRenderer();

	ChunkRenderer(const ChunkRenderer &) = delete;
	ChunkRenderer &operator=(const ChunkRenderer &) = delete;

	// Bring GPU state in line with `store`; mesh at most `budget` chunks.
	void sync(const world::ClientChunkStore &store, int budget = 4);

	// Draw every uploaded chunk. Call inside BeginMode3D/EndMode3D.
	void draw() const;

	std::size_t uploaded_count() const { return gpu_.size(); }

private:
	struct GpuChunk;
	void upload(const world::ClientChunkStore &store, core::ChunkCoord coord);
	void drop(core::ChunkCoord coord);

	std::unordered_map<core::ChunkCoord, GpuChunk> gpu_;
};

} // namespace vb::render
