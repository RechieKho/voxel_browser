#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

#include <raylib.h>

#include "vb/render/texture_atlas.hpp" // AtlasRect, VirtualFs
#include "vb/world/block.hpp"

// The crack-stage overlay atlas (REMAINING_TASKS.md 6.5's last piece: "real
// crack-stage texture art + crack_texture override"). Unlike TextureAtlas
// (exactly one cell per block), most blocks never appear in this atlas at
// all -- they share one built-in generic row of kStages cells; only a block
// with a valid vb.register_block{crack_texture=...} gets its own extra row.
// Same pure-build()/GPU-upload() split as TextureAtlas.

namespace vb::render {

class CrackAtlas {
public:
	// Number of progressive damage stages, index 0 (freshest hit) to
	// kStages-1 (about to break). Neither the spec nor REMAINING_TASKS pin an
	// exact count; 8 is an arbitrary "enough to read as progressive," not a
	// wire-format constant a pack ever needs to know -- a crack_texture
	// override's own frame count is inferred from its image width instead
	// (see build()).
	static constexpr int kStages = 8;
	static constexpr int kCellSize = TextureAtlas::kCellSize;

	// Pure CPU build. For every block in `registry` with a non-empty
	// `crack_texture` path present in `virtual_fs` and decodable into exactly
	// kStages equal-width square frames (image width == kStages * image
	// height, height > 0), slices and packs its own kStages-cell row. Every
	// other block (no override, missing from virtual_fs, or a
	// wrong-shaped/undecodable image) gets no row at all -- rect_for() falls
	// back to the one shared, procedurally-generated default row (a
	// deterministic, increasingly-dense crack-line pattern; no art tools
	// exist in this environment, same placeholder-first posture as every
	// other first pass in this codebase).
	static CrackAtlas build(const world::BlockRegistry &registry, const VirtualFs &virtual_fs);

	// GL-context-requiring, same posture/caveats as TextureAtlas::upload().
	Texture2D upload();

	// `stage` is clamped into [0, kStages). A block with no valid override
	// row gets the shared default row.
	AtlasRect rect_for(core::BlockId id, int stage) const;

private:
	Image image_{};
	std::vector<AtlasRect> default_rects_;
	std::unordered_map<std::uint32_t, std::vector<AtlasRect>> override_rects_;
};

} // namespace vb::render
