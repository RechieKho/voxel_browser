#include "vb/render/crack_atlas.hpp"

#include <algorithm>
#include <random>

namespace vb::render {

namespace {

constexpr int kCell = CrackAtlas::kCellSize;
// Only used from CrackAtlas::build()/rect_for(), both defined outside this
// anonymous namespace -- Clang's -Wunused-const-variable doesn't see those
// out-of-line uses and flags this as dead, unlike kCell above (used directly
// within draw_default_stage()/draw_override_stage() here).
[[maybe_unused]] constexpr int kStages = CrackAtlas::kStages;

// Deterministic (fixed seed per stage -- not real randomness, so a rebuild
// on any machine produces byte-identical stages) placeholder crack pattern:
// a handful of jittered line segments within the cell, growing denser and
// darker with stage. Not final art -- see header's own comment.
void draw_default_stage(Image &atlas, int content_x, int content_y, int stage) {
	std::minstd_rand rng(static_cast<unsigned int>(stage) + 1u);
	std::uniform_int_distribution<int> jitter(0, kCell - 1);
	const int line_count = 3 + stage * 2;
	const unsigned char alpha =
			static_cast<unsigned char>(std::min(60 + stage * 26, 255));
	const Color crack{ 10, 10, 10, alpha };
	for (int i = 0; i < line_count; ++i) {
		const int x0 = content_x + jitter(rng);
		const int y0 = content_y + jitter(rng);
		const int x1 = content_x + jitter(rng);
		const int y1 = content_y + jitter(rng);
		ImageDrawLine(&atlas, x0, y0, x1, y1, crack);
	}
}

// Crops the `frame`-th `frame_size`x`frame_size` square out of `sheet` and
// draws it (resized to kCell) into `atlas` at (content_x, content_y).
void draw_override_stage(Image &atlas, const Image &sheet, int frame_size,
		int frame, int content_x, int content_y) {
	Image cropped = ImageFromImage(sheet,
			Rectangle{ static_cast<float>(frame * frame_size), 0.0f,
					static_cast<float>(frame_size), static_cast<float>(frame_size) });
	ImageResize(&cropped, kCell, kCell);
	ImageDraw(&atlas, cropped,
			Rectangle{ 0, 0, static_cast<float>(kCell), static_cast<float>(kCell) },
			Rectangle{ static_cast<float>(content_x), static_cast<float>(content_y),
					static_cast<float>(kCell), static_cast<float>(kCell) },
			WHITE);
	UnloadImage(cropped);
}

AtlasRect rect_at(int col, int row, int atlas_w, int atlas_h) {
	const float x = static_cast<float>(col * kCell);
	const float y = static_cast<float>(row * kCell);
	return AtlasRect{
		x / static_cast<float>(atlas_w),
		y / static_cast<float>(atlas_h),
		(x + kCell) / static_cast<float>(atlas_w),
		(y + kCell) / static_cast<float>(atlas_h),
	};
}

} // namespace

CrackAtlas CrackAtlas::build(const world::BlockRegistry &registry, const VirtualFs &virtual_fs) {
	CrackAtlas atlas;

	struct OverrideSheet {
		core::BlockId id;
		Image decoded;
		int frame_size;
	};
	std::vector<OverrideSheet> overrides;
	for (std::size_t i = 0; i < registry.size(); ++i) {
		const auto id = static_cast<core::BlockId>(i);
		const world::BlockType &type = registry.get(id);
		if (type.crack_texture.empty()) {
			continue;
		}
		const auto it = virtual_fs.find(type.crack_texture);
		if (it == virtual_fs.end()) {
			continue;
		}
		Image decoded = LoadImageFromMemory(".png",
				reinterpret_cast<const unsigned char *>(it->second.data()),
				static_cast<int>(it->second.size()));
		if (decoded.data == nullptr) {
			continue;
		}
		if (decoded.height <= 0 || decoded.width != decoded.height * kStages) {
			UnloadImage(decoded);
			continue;
		}
		overrides.push_back({ id, decoded, decoded.height });
	}

	const int rows = 1 + static_cast<int>(overrides.size());
	const int atlas_w = kCell * kStages;
	const int atlas_h = kCell * rows;
	atlas.image_ = GenImageColor(atlas_w, atlas_h, BLANK);

	atlas.default_rects_.resize(static_cast<std::size_t>(kStages));
	for (int s = 0; s < kStages; ++s) {
		draw_default_stage(atlas.image_, s * kCell, 0, s);
		atlas.default_rects_[static_cast<std::size_t>(s)] = rect_at(s, 0, atlas_w, atlas_h);
	}

	for (std::size_t r = 0; r < overrides.size(); ++r) {
		const int row = 1 + static_cast<int>(r);
		std::vector<AtlasRect> rects(static_cast<std::size_t>(kStages));
		for (int s = 0; s < kStages; ++s) {
			draw_override_stage(atlas.image_, overrides[r].decoded, overrides[r].frame_size, s,
					s * kCell, row * kCell);
			rects[static_cast<std::size_t>(s)] = rect_at(s, row, atlas_w, atlas_h);
		}
		atlas.override_rects_[static_cast<std::uint32_t>(overrides[r].id)] = std::move(rects);
		UnloadImage(overrides[r].decoded);
	}

	return atlas;
}

CrackAtlas::~CrackAtlas() {
	if (image_.data != nullptr) {
		UnloadImage(image_);
	}
}

CrackAtlas::CrackAtlas(CrackAtlas &&other) noexcept
		: image_(other.image_),
		  default_rects_(std::move(other.default_rects_)),
		  override_rects_(std::move(other.override_rects_)) {
	other.image_ = Image{};
}

CrackAtlas &CrackAtlas::operator=(CrackAtlas &&other) noexcept {
	if (this != &other) {
		if (image_.data != nullptr) {
			UnloadImage(image_);
		}
		image_ = other.image_;
		default_rects_ = std::move(other.default_rects_);
		override_rects_ = std::move(other.override_rects_);
		other.image_ = Image{};
	}
	return *this;
}

Texture2D CrackAtlas::upload() {
	Texture2D tex = LoadTextureFromImage(image_);
	UnloadImage(image_);
	image_ = Image{};
	return tex;
}

AtlasRect CrackAtlas::rect_for(core::BlockId id, int stage) const {
	stage = std::clamp(stage, 0, kStages - 1);
	if (const auto it = override_rects_.find(static_cast<std::uint32_t>(id));
			it != override_rects_.end()) {
		return it->second[static_cast<std::size_t>(stage)];
	}
	return default_rects_[static_cast<std::size_t>(stage)];
}

} // namespace vb::render
