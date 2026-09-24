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

namespace {

// Phase 7.2: a faithful copy of raylib's own default mesh shader (see
// rlgl.h's RL_DEFAULT_SHADER_*), down to the attribute/uniform names it
// auto-wires by name (vertexPosition/vertexTexCoord/vertexNormal/
// vertexColor, mvp/matModel/colDiffuse/texture0) -- LoadShaderFromMemory
// detects those by name and fills Shader::locs itself, so raylib's own
// DrawMesh() keeps uploading mvp/matModel/colDiffuse/texture0 exactly as it
// would for the untouched default shader. Only the fog uniforms
// (fogViewPos/fogColor/fogStart/fogEnd, set once per frame by
// ChunkRenderer::set_fog) and the fog mix at the very end of main() are new.
constexpr const char *kFogVs = R"(#version 330
in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec3 vertexNormal;
in vec4 vertexColor;

uniform mat4 mvp;
uniform mat4 matModel;
uniform vec3 fogViewPos;

out vec2 fragTexCoord;
out vec4 fragColor;
out float fragFogDist;

void main()
{
    fragTexCoord = vertexTexCoord;
    fragColor = vertexColor;
    vec3 worldPos = vec3(matModel * vec4(vertexPosition, 1.0));
    fragFogDist = distance(worldPos, fogViewPos);
    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
)";

constexpr const char *kFogFs = R"(#version 330
in vec2 fragTexCoord;
in vec4 fragColor;
in float fragFogDist;

uniform sampler2D texture0;
uniform vec4 colDiffuse;
uniform vec3 fogColor;
uniform float fogStart;
uniform float fogEnd;

out vec4 finalColor;

void main()
{
    vec4 texelColor = texture(texture0, fragTexCoord);
    vec4 base = texelColor * colDiffuse * fragColor;
    float fogFactor = clamp((fogEnd - fragFogDist) / max(fogEnd - fogStart, 0.001), 0.0, 1.0);
    finalColor = vec4(mix(fogColor, base.rgb, fogFactor), base.a);
}
)";

} // namespace

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

// Writes `data`'s vertices/indices into the front of `mesh`'s (already
// allocated) CPU-side arrays. Those arrays may be larger than `data` needs
// (see model_from_mesh's headroom) -- only the first
// data.vertices.size()/indices.size() entries are touched; any tail is
// leftover from a previous, larger mesh and never referenced by the draw
// call (DrawMesh draws exactly mesh.triangleCount*3 indices, and every index
// value stays < data.vertices.size()).
//
// `rects` is empty until ChunkRenderer::set_atlas() has been called (no real
// texture system yet built, or a VB_WITH_COMPRESSION-less build where asset
// sync/the atlas never runs) -- that's today's Phase-2 behavior exactly:
// local 0/1 face UVs (raylib's default 1x1 white texture0 samples the same
// regardless) and a flat per-block vertex-color tint. Once an atlas exists,
// texcoords remap through the block's real AtlasRect and the vertex color
// drops to light-only (grayscale) -- the atlas texel itself now carries the
// color, so a real-textured block isn't double-tinted by the old guessed
// flat color. Alpha is untouched either way: still sourced from
// fallback_color_for()'s `.a` channel (leaves/water transparency), which was
// never meant to be replaced by per-texel alpha in this pass.
void fill_mesh_arrays(const world::MeshData &data, Mesh &mesh, const std::vector<AtlasRect> &rects) {
	for (std::size_t i = 0; i < data.vertices.size(); ++i) {
		const world::MeshVertex &v = data.vertices[i];
		mesh.vertices[i * 3 + 0] = v.px;
		mesh.vertices[i * 3 + 1] = v.py;
		mesh.vertices[i * 3 + 2] = v.pz;
		mesh.normals[i * 3 + 0] = v.nx;
		mesh.normals[i * 3 + 1] = v.ny;
		mesh.normals[i * 3 + 2] = v.nz;

		const Color base = fallback_color_for(v.block_id);
		const float l = v.light;
		if (rects.empty()) {
			mesh.texcoords[i * 2 + 0] = v.u;
			mesh.texcoords[i * 2 + 1] = v.v;
			mesh.colors[i * 4 + 0] = static_cast<unsigned char>(static_cast<float>(base.r) * l);
			mesh.colors[i * 4 + 1] = static_cast<unsigned char>(static_cast<float>(base.g) * l);
			mesh.colors[i * 4 + 2] = static_cast<unsigned char>(static_cast<float>(base.b) * l);
		} else {
			const AtlasRect &rect = v.block_id < rects.size() ? rects[v.block_id] : AtlasRect{};
			mesh.texcoords[i * 2 + 0] = rect.u0 + v.u * (rect.u1 - rect.u0);
			mesh.texcoords[i * 2 + 1] = rect.v0 + v.v * (rect.v1 - rect.v0);
			const auto lit = static_cast<unsigned char>(255.0f * l);
			mesh.colors[i * 4 + 0] = lit;
			mesh.colors[i * 4 + 1] = lit;
			mesh.colors[i * 4 + 2] = lit;
		}
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

Model model_from_mesh(const world::MeshData &data, std::size_t vertex_capacity, std::size_t index_capacity,
		const std::vector<AtlasRect> &rects) {
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

	fill_mesh_arrays(data, mesh, rects);

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
void update_gpu_mesh(Mesh &mesh, const world::MeshData &data, const std::vector<AtlasRect> &rects) {
	fill_mesh_arrays(data, mesh, rects);

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

ChunkRenderer::ChunkRenderer(std::size_t mesh_threads) : pool_(mesh_threads) {
	fog_shader_ = LoadShaderFromMemory(kFogVs, kFogFs);
	fog_loc_view_pos_ = GetShaderLocation(fog_shader_, "fogViewPos");
	fog_loc_color_ = GetShaderLocation(fog_shader_, "fogColor");
	fog_loc_start_ = GetShaderLocation(fog_shader_, "fogStart");
	fog_loc_end_ = GetShaderLocation(fog_shader_, "fogEnd");
}

ChunkRenderer::~ChunkRenderer() {
	for (auto &[coord, gpu] : gpu_) {
		(void)coord;
		if (gpu.valid) {
			UnloadModel(gpu.model);
		}
	}
	UnloadShader(fog_shader_);
	if (has_atlas_) {
		UnloadTexture(atlas_);
	}
}

void ChunkRenderer::set_atlas(Texture2D atlas, std::vector<AtlasRect> rects, std::vector<Color> average_colors) {
	if (has_atlas_) {
		UnloadTexture(atlas_);
	}
	atlas_ = atlas;
	has_atlas_ = true;
	atlas_rects_ = std::move(rects);
	atlas_average_colors_ = std::move(average_colors);
}

Color ChunkRenderer::underwater_tint(core::BlockId id) const {
	const auto idx = static_cast<std::size_t>(id);
	if (idx < atlas_average_colors_.size()) {
		return atlas_average_colors_[idx];
	}
	return fallback_color_for(static_cast<std::uint32_t>(idx));
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
		update_gpu_mesh(slot.model.meshes[0], data, atlas_rects_);
	} else {
		if (slot.valid) {
			UnloadModel(slot.model);
		}
		slot.vertex_capacity = capacity_for(needed_v);
		slot.index_capacity = capacity_for(needed_i);
		slot.model = model_from_mesh(data, slot.vertex_capacity, slot.index_capacity, atlas_rects_);
		// Phase 7.2: every chunk shares the one fog shader loaded in the
		// constructor -- LoadModelFromMesh (inside model_from_mesh) assigns
		// raylib's own default material/shader, which this replaces.
		slot.model.materials[0].shader = fog_shader_;
		// Real texture/atlas system: bind the shared atlas texture (a no-op,
		// still raylib's default white 1x1, until set_atlas() has been
		// called -- see the class's kLoading-time call site in
		// src/client/main.cpp).
		if (has_atlas_) {
			slot.model.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = atlas_;
		}
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

void ChunkRenderer::set_fog(core::Vec3d view_pos, world::SkyColor sky, float start, float end) const {
	const float view[3] = { static_cast<float>(view_pos.x),
		static_cast<float>(view_pos.y), static_cast<float>(view_pos.z) };
	const float color[3] = { static_cast<float>(sky.r) / 255.0f,
		static_cast<float>(sky.g) / 255.0f, static_cast<float>(sky.b) / 255.0f };
	SetShaderValue(fog_shader_, fog_loc_view_pos_, view, SHADER_UNIFORM_VEC3);
	SetShaderValue(fog_shader_, fog_loc_color_, color, SHADER_UNIFORM_VEC3);
	SetShaderValue(fog_shader_, fog_loc_start_, &start, SHADER_UNIFORM_FLOAT);
	SetShaderValue(fog_shader_, fog_loc_end_, &end, SHADER_UNIFORM_FLOAT);
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
