#pragma once

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

#include <raylib.h>

#include "vb/world/block.hpp"

// The real texture/atlas system (closes the long-standing "no real texture
// system in this engine at all" gap tracked across REMAINING_TASKS' Phase
// 4/5/6.5/7.5 items). One fixed-size cell per registered block id, packed
// into a single grid Image/Texture -- deterministic (block id order), no
// mip/rotation/trimming, good enough for a first real pipeline.
//
// Split in two, mirroring vb::world::mesh_chunk_from_snapshot's
// pure-function/GPU-upload split: build() is pure CPU (Image manipulation
// only, no GL context needed -- safe to unit test headless), upload() is the
// thin GPU step a real client calls once per session.

namespace vb::render {

struct AtlasRect {
	float u0 = 0.0f, v0 = 0.0f, u1 = 1.0f, v1 = 1.0f;
};

// The flat per-block placeholder color for a block with no real texture (or
// before any atlas has been built at all). See texture_atlas.cpp for the
// full rationale -- this used to be chunk_renderer.cpp's own Phase-2 tint_for().
Color fallback_color_for(std::uint32_t block_id);

// Pack-relative path -> raw file bytes, exactly vb::assetsync::ClientAssetCache
// ::virtual_fs()'s shape (kept as a raw type here so vb_render doesn't need to
// depend on vb_assetsync just for this one call).
using VirtualFs = std::unordered_map<std::string, std::vector<std::byte>>;

class TextureAtlas {
public:
	// Cell size in pixels for every block's slot in the atlas grid, real or
	// fallback. Chosen small enough to stay a placeholder scale, matches the
	// 16x16 placeholder textures content/base/textures ships.
	static constexpr int kCellSize = 16;

	// Pure CPU build: for every block id in `registry` with a non-empty
	// `texture` path found in `virtual_fs`, decodes it and draws it (resized
	// to kCellSize) into its cell, and records the real average pixel color.
	// Every other block id (no texture path, or the path isn't in
	// `virtual_fs`, or it fails to decode) gets a flat fallback cell/average
	// color instead (see fallback_color_for in the .cpp) -- this is
	// REMAINING_TASKS 7.5's documented interim placeholder, not a special
	// case callers need to branch on.
	//
	// Caller owns the returned Image (must UnloadImage it, or pass it to
	// upload() which does so).
	static TextureAtlas build(const world::BlockRegistry &registry, const VirtualFs &virtual_fs);

	// GL-context-requiring: uploads `image_` as a real GPU texture and frees
	// the CPU-side Image. Not unit tested (same posture as
	// ChunkRenderer::upload itself). Call at most once.
	Texture2D upload();

	const AtlasRect &rect_for(core::BlockId id) const;
	Color average_color_for(core::BlockId id) const;

	std::size_t block_count() const { return rects_.size(); }

private:
	Image image_{};
	std::vector<AtlasRect> rects_;
	std::vector<Color> average_colors_;
};

} // namespace vb::render
