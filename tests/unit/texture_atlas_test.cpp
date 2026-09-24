#include <doctest/doctest.h>

#include <cstddef>
#include <vector>

#include <raylib.h>

#include "vb/render/texture_atlas.hpp"
#include "vb/world/block.hpp"

using vb::render::AtlasRect;
using vb::render::TextureAtlas;
using vb::render::VirtualFs;
using vb::world::BlockRegistry;

namespace {

// Round-trips a solid-color Image through raylib's own PNG encoder so the
// bytes TextureAtlas::build() decodes are exactly what a real synced texture
// file would contain, not hand-rolled test-only bytes.
std::vector<std::byte> encode_png(Color color) {
	Image img = GenImageColor(4, 4, color);
	int size = 0;
	unsigned char *bytes = ExportImageToMemory(img, ".png", &size);
	UnloadImage(img);
	REQUIRE(bytes != nullptr);
	std::vector<std::byte> out(reinterpret_cast<std::byte *>(bytes), reinterpret_cast<std::byte *>(bytes) + size);
	MemFree(bytes);
	return out;
}

bool in_unit_range(const AtlasRect &r) {
	return r.u0 >= 0.0f && r.v0 >= 0.0f && r.u1 <= 1.0f && r.v1 <= 1.0f && r.u1 > r.u0 && r.v1 > r.v0;
}

} // namespace

TEST_CASE("TextureAtlas::build gives a real texture its own rect and average color") {
	BlockRegistry registry = BlockRegistry::base(); // air, stone, dirt, grass, sand, water, wood, leaves

	VirtualFs vfs;
	vfs["textures/red.png"] = encode_png(Color{ 200, 20, 20, 255 });

	registry.set_texture(vb::world::base_block::stone, "textures/red.png");

	const TextureAtlas atlas = TextureAtlas::build(registry, vfs);
	REQUIRE(atlas.block_count() == registry.size());

	const Color avg = atlas.average_color_for(vb::world::base_block::stone);
	CHECK(avg.r > 150); // dominated by the red fill, not the fallback gray
	CHECK(avg.g < 80);
	CHECK(avg.b < 80);

	CHECK(in_unit_range(atlas.rect_for(vb::world::base_block::stone)));
}

TEST_CASE("TextureAtlas::build falls back to the flat placeholder color when a block has no texture") {
	BlockRegistry registry = BlockRegistry::base();
	const VirtualFs empty_vfs;

	const TextureAtlas atlas = TextureAtlas::build(registry, empty_vfs);

	// base:dirt (id 2) never gets a texture in this pass -- its average
	// should be exactly the flat placeholder tint, not an arbitrary color.
	CHECK(atlas.average_color_for(vb::world::base_block::dirt).r ==
			vb::render::fallback_color_for(static_cast<std::uint32_t>(vb::world::base_block::dirt)).r);
	CHECK(atlas.average_color_for(vb::world::base_block::dirt).g ==
			vb::render::fallback_color_for(static_cast<std::uint32_t>(vb::world::base_block::dirt)).g);
	CHECK(atlas.average_color_for(vb::world::base_block::dirt).b ==
			vb::render::fallback_color_for(static_cast<std::uint32_t>(vb::world::base_block::dirt)).b);
}

TEST_CASE("TextureAtlas::build degrades gracefully when a block's texture path is missing from virtual_fs") {
	BlockRegistry registry = BlockRegistry::base();

	registry.set_texture(vb::world::base_block::stone, "textures/never_synced.png"); // not present below

	const VirtualFs empty_vfs;
	const TextureAtlas atlas = TextureAtlas::build(registry, empty_vfs); // must not crash

	const Color avg = atlas.average_color_for(vb::world::base_block::stone);
	const Color fallback = vb::render::fallback_color_for(static_cast<std::uint32_t>(vb::world::base_block::stone));
	CHECK(avg.r == fallback.r);
	CHECK(avg.g == fallback.g);
	CHECK(avg.b == fallback.b);
}

TEST_CASE("TextureAtlas::build packs every block into a non-overlapping, in-range rect") {
	BlockRegistry registry = BlockRegistry::base();
	const VirtualFs empty_vfs;
	const TextureAtlas atlas = TextureAtlas::build(registry, empty_vfs);

	std::vector<AtlasRect> rects;
	for (std::size_t i = 0; i < atlas.block_count(); ++i) {
		const AtlasRect &r = atlas.rect_for(static_cast<vb::core::BlockId>(i));
		CHECK(in_unit_range(r));
		rects.push_back(r);
	}
	for (std::size_t i = 0; i < rects.size(); ++i) {
		for (std::size_t j = i + 1; j < rects.size(); ++j) {
			const bool disjoint = rects[i].u1 <= rects[j].u0 || rects[j].u1 <= rects[i].u0 ||
					rects[i].v1 <= rects[j].v0 || rects[j].v1 <= rects[i].v0;
			CHECK(disjoint);
		}
	}
}
