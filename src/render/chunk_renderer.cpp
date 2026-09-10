#include "vb/render/chunk_renderer.hpp"

#include <array>
#include <cstring>
#include <vector>

#include <raylib.h>

#include "vb/core/ids.hpp"
#include "vb/world/block.hpp"
#include "vb/world/chunk.hpp"
#include "vb/world/chunk_mesher.hpp"

namespace vb::render {

struct ChunkRenderer::GpuChunk {
	Model model{};
	std::uint64_t revision = 0;
	bool valid = false;
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

Model model_from_mesh(const world::MeshData &data) {
	Mesh mesh{};
	mesh.vertexCount = static_cast<int>(data.vertices.size());
	mesh.triangleCount = static_cast<int>(data.indices.size() / 3);

	mesh.vertices = static_cast<float *>(
			MemAlloc(static_cast<unsigned int>(data.vertices.size() * 3 * sizeof(float))));
	mesh.normals = static_cast<float *>(
			MemAlloc(static_cast<unsigned int>(data.vertices.size() * 3 * sizeof(float))));
	mesh.texcoords = static_cast<float *>(
			MemAlloc(static_cast<unsigned int>(data.vertices.size() * 2 * sizeof(float))));
	mesh.colors = static_cast<unsigned char *>(
			MemAlloc(static_cast<unsigned int>(data.vertices.size() * 4)));
	mesh.indices = static_cast<unsigned short *>(
			MemAlloc(static_cast<unsigned int>(data.indices.size() * sizeof(unsigned short))));

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

	UploadMesh(&mesh, false);
	return LoadModelFromMesh(mesh);
}

} // namespace

ChunkRenderer::ChunkRenderer() = default;

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

void ChunkRenderer::upload(const world::ClientChunkStore &store,
		core::ChunkCoord coord) {
	const world::Chunk *chunk = store.find(coord);
	if (chunk == nullptr) {
		return;
	}
	const world::MeshData data = world::mesh_chunk(store, coord);

	GpuChunk &slot = gpu_[coord];
	if (slot.valid) {
		UnloadModel(slot.model);
		slot.valid = false;
	}
	if (!data.empty()) {
		slot.model = model_from_mesh(data);
		slot.valid = true;
	}
	slot.revision = chunk->revision();
}

void ChunkRenderer::sync(const world::ClientChunkStore &store, int budget) {
	// Drop meshes for chunks that unloaded.
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

	// (Re)mesh new / changed chunks, up to the budget.
	int done = 0;
	for (core::ChunkCoord coord : store.loaded_coords()) {
		if (done >= budget) {
			break;
		}
		const auto it = gpu_.find(coord);
		const world::Chunk *chunk = store.find(coord);
		const bool needs = it == gpu_.end() ||
				it->second.revision != chunk->revision();
		if (needs) {
			upload(store, coord);
			++done;
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
