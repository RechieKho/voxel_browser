#pragma once

#include <cstdint>
#include <vector>

#include "vb/core/ids.hpp"
#include "vb/world/block.hpp"
#include "vb/world/client_chunk_store.hpp"

// Chunk meshing (spec §11.2). Renderer-neutral output — the client converts
// MeshData to a raylib Mesh at upload time. Face-culled cube geometry with
// per-vertex light + ambient occlusion; greedy merging is the Cellulose
// `greedy_mesh` swap-in behind VB_WITH_MESHING (spec §19 Q2). Same inputs and
// output shape so that swap is local.

namespace vb::world {

struct MeshVertex {
	float px = 0, py = 0, pz = 0;
	float nx = 0, ny = 0, nz = 0;
	float u = 0, v = 0;
	float light = 1.0f; // 0..1, combined sky/block light * AO
	std::uint32_t block_id = 0;

	bool operator==(const MeshVertex &) const = default;
};

struct MeshData {
	std::vector<MeshVertex> vertices;
	std::vector<std::uint32_t> indices; // triangle triples, CCW front

	bool empty() const { return vertices.empty(); }
	std::size_t quad_count() const { return indices.size() / 6; }
};

// Build the opaque mesh for one loaded chunk. Reads neighbour voxels/light from
// `store` (across chunk borders), so re-mesh a chunk when a neighbour arrives.
// Returns an empty mesh if the chunk is not loaded.
MeshData mesh_chunk(const ClientChunkStore &store, core::ChunkCoord coord);

} // namespace vb::world
