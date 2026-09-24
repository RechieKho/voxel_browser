#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <raylib.h>

#include "vb/core/ids.hpp"
#include "vb/core/math.hpp"
#include "vb/render/texture_atlas.hpp"
#include "vb/world/chunk_mesh_worker_pool.hpp"
#include "vb/world/client_chunk_store.hpp"
#include "vb/world/daynight.hpp" // SkyColor

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

	// Phase 7.2: sets the fog uniforms every uploaded chunk's material shares
	// (the shader itself, loaded once in the constructor, blends fragment
	// color to `sky` between world-space distances `start` and `end` from
	// `view_pos`). Call once per frame, any time before draw() -- no
	// BeginMode3D/EndMode3D requirement, unlike draw() itself.
	void set_fog(core::Vec3d view_pos, world::SkyColor sky, float start, float end) const;

	// Draw every uploaded chunk. Call inside BeginMode3D/EndMode3D.
	void draw() const;

	std::size_t uploaded_count() const { return gpu_.size(); }
	std::size_t pending_mesh_count() const { return pool_.pending(); }

	// Real texture/atlas system: hands over ownership of a GPU atlas texture
	// (already uploaded, e.g. via TextureAtlas::upload()) plus its per-block
	// UV rects/average colors. Every chunk model created *after* this call
	// binds the atlas as its material's diffuse texture and remaps face UVs
	// through `rects`; models already uploaded before this call are left
	// alone until they're next rebuilt (upload() always re-fills the CPU
	// arrays from the current rects_/has_atlas_ state, so any live chunk
	// naturally picks up the atlas on its next edit-triggered re-mesh -- see
	// src/client/main.cpp, which calls this once per session before any
	// chunk has been meshed at all, so in practice every chunk gets it from
	// its very first upload). Call at most once per ChunkRenderer.
	void set_atlas(Texture2D atlas, std::vector<AtlasRect> rects, std::vector<Color> average_colors);

	// REMAINING_TASKS 7.5: the color underwater fog should default to for
	// `id` -- the atlas's real average texture color if set_atlas() has been
	// called and covers `id`, else the same flat placeholder chunk meshes
	// themselves fall back to.
	Color underwater_tint(core::BlockId id) const;

private:
	struct GpuChunk;
	void upload(core::ChunkCoord coord, const world::MeshData &data, std::uint64_t revision);
	void drop(core::ChunkCoord coord);

	// Phase 7.2: one shader shared by every chunk's material (assigned in
	// upload()) -- a faithful reproduction of raylib's default mesh shader
	// (texture0 * colDiffuse * vertex color) plus a linear fog mix at the
	// end, rather than raylib's own default shader untouched. Loaded once in
	// the constructor (a GL context is guaranteed to exist by then -- see
	// ChunkRenderer's only construction site in src/client/main.cpp, always
	// after window init) and unloaded in the destructor.
	Shader fog_shader_{};
	int fog_loc_view_pos_ = -1;
	int fog_loc_color_ = -1;
	int fog_loc_start_ = -1;
	int fog_loc_end_ = -1;

	Texture2D atlas_{};
	bool has_atlas_ = false;
	std::vector<AtlasRect> atlas_rects_;
	std::vector<Color> atlas_average_colors_;

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
