#include "vb/render/chunk_renderer.hpp"

#include <array>
#include <cstring>
#include <vector>

#include <raylib.h>

#include "vb/core/ids.hpp"
#include "vb/world/block.hpp"
#include "vb/world/chunk.hpp"
#include "vb/world/chunk_mesh_snapshot.hpp"
#include "vb/world/chunk_mesher.hpp"

namespace vb::render {

struct ChunkRenderer::GpuChunk {
	Model model{};
	std::uint64_t revision = 0;
	bool valid = false;
	// GPU buffer capacity in elements, >= what's currently drawn (see
	// model_from_mesh's headroom below). A re-mesh whose new vertex/index
	// count still fits reuses these buffers via UpdateMeshBuffer() instead of
	// a full UnloadModel+recreate -- see ChunkRenderer::upload().
	std::size_t vertex_capacity = 0;
	std::size_t index_capacity = 0;
};

namespace {

// Flat per-block tint for Phase 2 (the texture atlas is Phase 4).
Color tint_for(std::uint32_t block_id) {
	switch (block_id) {
		case 1:
			return Color{ 128, 128, 132, 255 }; // stone
		case 2:
			return Color{ 134, 96, 67, 255 }; // dirt
		case 3:
			return Color{ 96, 160, 74, 255 }; // grass
		case 4:
			return Color{ 214, 200, 150, 255 }; // sand
		case 5:
			return Color{ 64, 108, 196, 200 }; // water
		case 6:
			return Color{ 110, 84, 52, 255 }; // wood
		case 7:
			return Color{ 74, 128, 60, 220 }; // leaves
		default:
			return WHITE;
	}
}

// Writes `data`'s vertices/indices into the front of `mesh`'s (already
// allocated) CPU-side arrays. Those arrays may be larger than `data` needs
// (see model_from_mesh's headroom) -- only the first
// data.vertices.size()/indices.size() entries are touched; any tail is
// leftover from a previous, larger mesh and never referenced by the draw
// call (DrawMesh draws exactly mesh.triangleCount*3 indices, and every index
// value stays < data.vertices.size()).
void fill_mesh_arrays(const world::MeshData &data, Mesh &mesh) {
	for (std::size_t i = 0; i < data.vertices.size(); ++i) {
		const world::MeshVertex &v = data.vertices[i];
		mesh.vertices[i * 3 + 0] = v.px;
		mesh.vertices[i * 3 + 1] = v.py;
		mesh.vertices[i * 3 + 2] = v.pz;
		mesh.normals[i * 3 + 0] = v.nx;
		mesh.normals[i * 3 + 1] = v.ny;
		mesh.normals[i * 3 + 2] = v.nz;
		mesh.texcoords[i * 2 + 0] = v.u;
		mesh.texcoords[i * 2 + 1] = v.v;

		const Color base = tint_for(v.block_id);
		const float l = v.light;
		mesh.colors[i * 4 + 0] = static_cast<unsigned char>(static_cast<float>(base.r) * l);
		mesh.colors[i * 4 + 1] = static_cast<unsigned char>(static_cast<float>(base.g) * l);
		mesh.colors[i * 4 + 2] = static_cast<unsigned char>(static_cast<float>(base.b) * l);
		mesh.colors[i * 4 + 3] = base.a;
	}
	for (std::size_t i = 0; i < data.indices.size(); ++i) {
		mesh.indices[i] = static_cast<unsigned short>(data.indices[i]);
	}
}

// New GPU buffers are allocated with slack above what's needed right now, so
// a later re-mesh that grows a little (the common case -- editing a block
// changes face count by a handful of quads) can reuse them via
// UpdateMeshBuffer() instead of forcing a full unload/reload. See
// ChunkRenderer::upload() and the STATE.md write-up on the NVIDIA driver's
// VAO/VBO-churn heap corruption this is mitigating.
constexpr float kCapacityHeadroom = 1.25f;

std::size_t capacity_for(std::size_t needed) {
	return needed == 0 ? 0 : static_cast<std::size_t>(static_cast<float>(needed) * kCapacityHeadroom) + 1;
}

Model model_from_mesh(const world::MeshData &data, std::size_t vertex_capacity, std::size_t index_capacity) {
	Mesh mesh{};
	// vertexCount/triangleCount drive the GPU buffer *size* UploadMesh()
	// allocates below; they're brought back down to the actual counts
	// afterwards so DrawMesh only ever draws real geometry (see fill_mesh_arrays).
	mesh.vertexCount = static_cast<int>(vertex_capacity);
	mesh.triangleCount = static_cast<int>(index_capacity / 3);

	mesh.vertices = static_cast<float *>(MemAlloc(static_cast<unsigned int>(vertex_capacity * 3 * sizeof(float))));
	mesh.normals = static_cast<float *>(MemAlloc(static_cast<unsigned int>(vertex_capacity * 3 * sizeof(float))));
	mesh.texcoords = static_cast<float *>(MemAlloc(static_cast<unsigned int>(vertex_capacity * 2 * sizeof(float))));
	mesh.colors = static_cast<unsigned char *>(MemAlloc(static_cast<unsigned int>(vertex_capacity * 4)));
	mesh.indices = static_cast<unsigned short *>(
			MemAlloc(static_cast<unsigned int>(index_capacity * sizeof(unsigned short))));

	fill_mesh_arrays(data, mesh);

	UploadMesh(&mesh, false);
	mesh.vertexCount = static_cast<int>(data.vertices.size());
	mesh.triangleCount = static_cast<int>(data.indices.size() / 3);
	return LoadModelFromMesh(mesh);
}

// Re-fills an already-uploaded model's GPU buffers in place (glBufferSubData
// via UpdateMeshBuffer, no VAO/VBO reallocation) -- caller must already know
// `data` fits within the model's current vertex_capacity/index_capacity.
// Buffer index order matches raylib's UploadMesh(): 0 position, 1 texcoord,
// 2 normal, 3 color, 6 indices (config.h's RL_DEFAULT_SHADER_ATTRIB_LOCATION_*).
void update_gpu_mesh(Mesh &mesh, const world::MeshData &data) {
	fill_mesh_arrays(data, mesh);

	const int vbytes3 = static_cast<int>(data.vertices.size() * 3 * sizeof(float));
	const int vbytes2 = static_cast<int>(data.vertices.size() * 2 * sizeof(float));
	UpdateMeshBuffer(mesh, 0, mesh.vertices, vbytes3, 0);
	UpdateMeshBuffer(mesh, 1, mesh.texcoords, vbytes2, 0);
	UpdateMeshBuffer(mesh, 2, mesh.normals, vbytes3, 0);
	UpdateMeshBuffer(mesh, 3, mesh.colors, static_cast<int>(data.vertices.size() * 4), 0);
	UpdateMeshBuffer(mesh, 6, mesh.indices, static_cast<int>(data.indices.size() * sizeof(unsigned short)), 0);

	mesh.vertexCount = static_cast<int>(data.vertices.size());
	mesh.triangleCount = static_cast<int>(data.indices.size() / 3);
}

} // namespace

ChunkRenderer::ChunkRenderer(std::size_t mesh_threads) : pool_(mesh_threads) {}

ChunkRenderer::~ChunkRenderer() {
	for (auto &[coord, gpu] : gpu_) {
		(void)coord;
		if (gpu.valid) {
			UnloadModel(gpu.model);
		}
	}
}

void ChunkRenderer::drop(core::ChunkCoord coord) {
	const auto it = gpu_.find(coord);
	if (it == gpu_.end()) {
		return;
	}
	if (it->second.valid) {
		UnloadModel(it->second.model);
	}
	gpu_.erase(it);
}

void ChunkRenderer::upload(core::ChunkCoord coord, const world::MeshData &data,
		std::uint64_t revision) {
	GpuChunk &slot = gpu_[coord];
	if (data.empty()) {
		if (slot.valid) {
			UnloadModel(slot.model);
		}
		slot = GpuChunk{};
		slot.revision = revision;
		return;
	}

	const std::size_t needed_v = data.vertices.size();
	const std::size_t needed_i = data.indices.size();
	if (slot.valid && needed_v <= slot.vertex_capacity && needed_i <= slot.index_capacity) {
		// Fits within the existing GPU buffers -- update in place, no
		// UnloadModel/UploadMesh churn (see the comment on GpuChunk).
		update_gpu_mesh(slot.model.meshes[0], data);
	} else {
		if (slot.valid) {
			UnloadModel(slot.model);
		}
		slot.vertex_capacity = capacity_for(needed_v);
		slot.index_capacity = capacity_for(needed_i);
		slot.model = model_from_mesh(data, slot.vertex_capacity, slot.index_capacity);
		slot.valid = true;
	}
	slot.revision = revision;
}

void ChunkRenderer::sync(const world::ClientChunkStore &store, int submit_budget, int upload_budget) {
	// Drop GPU state for chunks that unloaded.
	std::vector<core::ChunkCoord> gone;
	for (const auto &[coord, gpu] : gpu_) {
		(void)gpu;
		if (!store.has(coord)) {
			gone.push_back(coord);
		}
	}
	for (core::ChunkCoord c : gone) {
		drop(c);
	}

	// Queue whatever background meshing finished since the last call; these
	// aren't uploaded yet, just appended behind anything already waiting.
	for (world::ChunkMeshResult &result : pool_.poll_completed()) {
		pending_coords_.insert(result.coord);
		pending_uploads_.push_back(std::move(result));
	}

	// GPU-upload at most `upload_budget` queued results this frame. A result
	// is stale if the chunk changed again after its snapshot was taken (or
	// unloaded while queued) -- drop it silently without counting against the
	// budget; the chunk's current revision won't match gpu_[coord].revision,
	// so the submit loop below requeues it.
	int uploaded = 0;
	while (uploaded < upload_budget && !pending_uploads_.empty()) {
		world::ChunkMeshResult result = std::move(pending_uploads_.front());
		pending_uploads_.pop_front();
		pending_coords_.erase(result.coord);
		const world::Chunk *chunk = store.find(result.coord);
		if (chunk == nullptr || chunk->revision() != result.revision) {
			continue; // unloaded or superseded since the snapshot was built
		}
		upload(result.coord, result.mesh, result.revision);
		++uploaded;
	}

	// Submit new / changed chunks for background meshing, up to the budget.
	int submitted = 0;
	for (core::ChunkCoord coord : store.loaded_coords()) {
		if (submitted >= submit_budget) {
			break;
		}
		const world::Chunk *chunk = store.find(coord);
		const auto it = gpu_.find(coord);
		const bool needs = it == gpu_.end() || it->second.revision != chunk->revision();
		if (!needs || pool_.in_flight_or_queued(coord) || pending_coords_.contains(coord)) {
			continue;
		}
		world::ChunkMeshSnapshot snapshot = world::build_chunk_mesh_snapshot(store, coord);
		if (pool_.submit(std::move(snapshot), store.registry())) {
			++submitted;
		}
	}
}

void ChunkRenderer::draw() const {
	for (const auto &[coord, gpu] : gpu_) {
		if (!gpu.valid) {
			continue;
		}
		const core::IVec3 o = core::chunk_origin(coord);
		DrawModel(gpu.model,
				Vector3{ static_cast<float>(o.x), static_cast<float>(o.y),
						static_cast<float>(o.z) },
				1.0f, WHITE);
	}
}

} // namespace vb::render
