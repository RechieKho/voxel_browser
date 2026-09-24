#include "vb/render/texture_atlas.hpp"

#include <algorithm>
#include <cmath>

#include <rlgl.h>

namespace vb::render {

// The pre-atlas flat per-block color (originally chunk_renderer.cpp's
// Phase-2-era tint_for()). Two jobs now: (1) the vertex-color tint
// ChunkRenderer still applies when no atlas has been set at all (build
// without VB_WITH_COMPRESSION, or before the client's first atlas build),
// and (2) the fallback cell color/average for any block id that has no real
// texture -- REMAINING_TASKS 7.5's documented interim placeholder ("a
// flat-colored block has zero texture variance... a naive default built
// directly off today's hardcoded switch is a reasonable placeholder").
Color fallback_color_for(std::uint32_t block_id) {
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
			return Color{ 64, 108, 196, 255 }; // water
		case 6:
			return Color{ 110, 84, 52, 255 }; // wood
		case 7:
			return Color{ 74, 128, 60, 220 }; // leaves
		default:
			return WHITE;
	}
}

namespace {

// Average pixel color of a decoded Image, ignoring fully transparent pixels
// (a transparent border shouldn't wash out a texture's own color).
Color average_of(const Image &img) {
	Color *pixels = LoadImageColors(img);
	std::uint64_t r = 0, g = 0, b = 0, count = 0;
	const int n = img.width * img.height;
	for (int i = 0; i < n; ++i) {
		if (pixels[i].a == 0) {
			continue;
		}
		r += pixels[i].r;
		g += pixels[i].g;
		b += pixels[i].b;
		++count;
	}
	UnloadImageColors(pixels);
	if (count == 0) {
		return WHITE;
	}
	return Color{
		static_cast<unsigned char>(r / count),
		static_cast<unsigned char>(g / count),
		static_cast<unsigned char>(b / count),
		255,
	};
}

int grid_dim(std::size_t block_count) {
	return static_cast<int>(std::ceil(std::sqrt(static_cast<double>(std::max<std::size_t>(block_count, 1)))));
}

// Bilinear texture filtering (and plain UV-rounding right at a cell
// boundary) samples a texel or two past the edge of its own rect -- with
// cells packed edge-to-edge that reaches into the *neighbouring* cell,
// which is often a very different flat fallback color, and shows up as a
// grayish seam along every block edge. The standard atlas fix: give each
// cell a padding border, filled by clamping (duplicating) its own edge
// pixels outward, so any sampling that spills past the content rect still
// reads the same color instead of the neighbour's. AtlasRect only ever
// covers the inner, unpadded kCellSize x kCellSize content.
//
// kPadding is 4, not 1: TextureAtlas::upload() generates real GPU mipmaps
// (see its own comment -- fixes a second, distinct artifact, un-mipmapped
// minification shimmer on distant/grazing surfaces like open water) and
// each mip level halves resolution via a box filter, which needs its own
// margin of "safely our own color, not the neighbour's" pixels to still be
// true a few halvings down: level0's 4px padding -> level1 still has ~2px,
// level2 ~1px, so only mip levels finer than that stay bleed-free. Anything
// coarser is such extreme minification (looking almost straight down the
// horizon) that fog has already mixed the fragment down to the fog color
// regardless, so residual bleeding there is not visible in practice.
constexpr int kPadding = 4;
constexpr int kStride = TextureAtlas::kCellSize + 2 * kPadding;

// Extends `img`'s edges by kPadding px outward from the kCellSize content
// rect at (content_x, content_y), clamping (nearest-edge-pixel) style —
// including corners, via the first (row) loop's x range already spanning
// the full padded width.
void extend_border(Image &img, int content_x, int content_y) {
	constexpr int s = TextureAtlas::kCellSize;
	for (int dx = -kPadding; dx < s + kPadding; ++dx) {
		const int sx = content_x + std::clamp(dx, 0, s - 1);
		const Color top = GetImageColor(img, sx, content_y);
		const Color bottom = GetImageColor(img, sx, content_y + s - 1);
		for (int p = 1; p <= kPadding; ++p) {
			ImageDrawPixel(&img, content_x + dx, content_y - p, top);
			ImageDrawPixel(&img, content_x + dx, content_y + s - 1 + p, bottom);
		}
	}
	for (int y = 0; y < s; ++y) {
		const Color left = GetImageColor(img, content_x, content_y + y);
		const Color right = GetImageColor(img, content_x + s - 1, content_y + y);
		for (int p = 1; p <= kPadding; ++p) {
			ImageDrawPixel(&img, content_x - p, content_y + y, left);
			ImageDrawPixel(&img, content_x + s - 1 + p, content_y + y, right);
		}
	}
}

} // namespace

TextureAtlas TextureAtlas::build(const world::BlockRegistry &registry, const VirtualFs &virtual_fs) {
	TextureAtlas atlas;
	const std::size_t n = registry.size();
	const int cols = grid_dim(n);
	const int rows = cols == 0 ? 0 : static_cast<int>((n + static_cast<std::size_t>(cols) - 1) / static_cast<std::size_t>(cols));
	const int atlas_w = std::max(cols, 1) * kStride;
	const int atlas_h = std::max(rows, 1) * kStride;

	atlas.image_ = GenImageColor(atlas_w, atlas_h, BLANK);
	atlas.rects_.resize(n);
	atlas.average_colors_.resize(n);

	for (std::size_t i = 0; i < n; ++i) {
		const auto id = static_cast<core::BlockId>(i);
		const int col = static_cast<int>(i) % cols;
		const int row = static_cast<int>(i) / cols;
		// The padded cell (fallback fill target) vs. the inner content rect
		// (real-texture draw target / the only thing AtlasRect ever maps).
		const Rectangle padded_cell{ static_cast<float>(col * kStride), static_cast<float>(row * kStride),
			static_cast<float>(kStride), static_cast<float>(kStride) };
		const int content_x = col * kStride + kPadding;
		const int content_y = row * kStride + kPadding;
		const Rectangle content{ static_cast<float>(content_x), static_cast<float>(content_y),
			static_cast<float>(kCellSize), static_cast<float>(kCellSize) };
		atlas.rects_[i] = AtlasRect{
			content.x / static_cast<float>(atlas_w),
			content.y / static_cast<float>(atlas_h),
			(content.x + content.width) / static_cast<float>(atlas_w),
			(content.y + content.height) / static_cast<float>(atlas_h),
		};

		const world::BlockType &type = registry.get(id);
		bool drew_real_texture = false;
		if (!type.texture.empty()) {
			if (const auto it = virtual_fs.find(type.texture); it != virtual_fs.end()) {
				Image decoded = LoadImageFromMemory(".png", reinterpret_cast<const unsigned char *>(it->second.data()),
						static_cast<int>(it->second.size()));
				if (decoded.data != nullptr) {
					atlas.average_colors_[i] = average_of(decoded);
					ImageResize(&decoded, kCellSize, kCellSize);
					ImageDraw(&atlas.image_, decoded, Rectangle{ 0, 0, static_cast<float>(decoded.width), static_cast<float>(decoded.height) },
							content, WHITE);
					UnloadImage(decoded);
					extend_border(atlas.image_, content_x, content_y);
					drew_real_texture = true;
				}
			}
		}
		if (!drew_real_texture) {
			// A flat fill needs no border extension -- padding included, it's
			// already the same uniform color any spill-over would sample.
			const Color fallback = fallback_color_for(static_cast<std::uint32_t>(i));
			atlas.average_colors_[i] = fallback;
			ImageDrawRectangleRec(&atlas.image_, padded_cell, fallback);
		}
	}

	return atlas;
}

Texture2D TextureAtlas::upload() {
	Texture2D tex = LoadTextureFromImage(image_);
	UnloadImage(image_);
	image_ = Image{};
	// Without mipmaps, GL_NEAREST samples exactly one texel per screen pixel
	// regardless of how much texture-space area that pixel actually covers --
	// on a distant or grazing-angle surface (open water is the worst case:
	// nearly edge-on to the camera near the horizon) many texels collapse
	// into one screen pixel, and picking just one of them at random looks
	// like sparkly/shimmering noise. Mipmaps give the GPU a pre-averaged,
	// correctly-blurred version to sample from at that distance instead.
	GenTextureMipmaps(&tex);
	// SetTextureFilter(TEXTURE_FILTER_BILINEAR) was tried first and rejected:
	// with mipmaps present it maps to GL_LINEAR_MIPMAP_NEAREST (raylib's
	// "sharp switching between mip levels"), which picks one whole mip level
	// per fragment with no blend at the boundary -- across a continuous
	// terrain surface that boundary is a screen-space *line* where every
	// fragment on one side jumps to a visibly different (coarser, more
	// bled-together) mip level than the other side. That's the "white lines
	// at far places" artifact. Setting MIN_FILTER to GL_LINEAR_MIPMAP_LINEAR
	// directly (true trilinear) blends the two nearest mip levels, so the
	// transition is gradual and the line disappears; it does mean a
	// fragment can now sample partly from a coarser mip's own residual
	// cross-cell bleed (kPadding's comment) a bit sooner, which is a much
	// smaller visual cost than a hard seam.
	//
	// MAG_FILTER is set to GL_NEAREST (not GL_LINEAR, what SetTextureFilter
	// would also set) rather than through SetTextureFilter -- these 16x16
	// placeholder textures are pixel art; bilinear-magnifying them onto a
	// close-up block face is exactly what read as "the stone block is
	// blurry." Minification (mip selection, above) and magnification are
	// independent GL states, so nearest-mag + trilinear-min is possible even
	// though no raylib TEXTURE_FILTER_* preset combines them -- call
	// rlTextureParameters directly instead of SetTextureFilter.
	rlTextureParameters(tex.id, RL_TEXTURE_MIN_FILTER, RL_TEXTURE_FILTER_MIP_LINEAR);
	rlTextureParameters(tex.id, RL_TEXTURE_MAG_FILTER, RL_TEXTURE_FILTER_NEAREST);
	// Trilinear (above) is isotropic -- it picks one mip level per fragment
	// based on the *worst-case* minification axis. A near-horizontal surface
	// (open water is again the worst case) is minified far more along the
	// screen's vertical axis than its horizontal one, so trilinear must pick
	// a mip level coarse enough for the vertical axis, over-blurring the
	// horizontal one; the water texture's own residual high-frequency detail
	// within that over-blurred axis still aliases, and because the two
	// axes' minification ratio changes continuously with distance under
	// perspective, the resulting alias pattern forms the concentric arcs
	// visible across the lake (iso-minification-ratio contours on a ground
	// plane are literally arcs). Anisotropic filtering samples along the
	// true minification direction instead of assuming it's uniform, which
	// is the actual fix. rlTextureParameters resets anisotropy to 1.0 on
	// *every* call (see rlgl.h's rlTextureParameters), including the
	// MIN/MAG_FILTER calls above -- this must run last, and any future
	// filter change on this texture must re-issue it afterward too.
	rlTextureParameters(tex.id, RL_TEXTURE_FILTER_ANISOTROPIC, 16);
	return tex;
}

const AtlasRect &TextureAtlas::rect_for(core::BlockId id) const {
	static constexpr AtlasRect kFull{ 0.0f, 0.0f, 1.0f, 1.0f };
	const auto idx = static_cast<std::size_t>(id);
	return idx < rects_.size() ? rects_[idx] : kFull;
}

Color TextureAtlas::average_color_for(core::BlockId id) const {
	const auto idx = static_cast<std::size_t>(id);
	return idx < average_colors_.size() ? average_colors_[idx] : fallback_color_for(static_cast<std::uint32_t>(idx));
}

} // namespace vb::render
