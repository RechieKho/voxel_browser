#include <doctest/doctest.h>

#include <cstddef>
#include <vector>

#include <raylib.h>

#include "vb/render/crack_atlas.hpp"
#include "vb/world/block.hpp"

using vb::render::AtlasRect;
using vb::render::CrackAtlas;
using vb::render::VirtualFs;
using vb::world::BlockRegistry;

namespace {

// Encodes a real spritesheet of `frames` equal-width square cells, each
// filled with a distinct flat color, through raylib's own PNG encoder --
// same round-trip-through-real-bytes posture as texture_atlas_test.cpp's own
// encode_png().
std::vector<std::byte> encode_stage_sheet(int frames, int frame_size) {
	Image sheet = GenImageColor(frames * frame_size, frame_size, BLANK);
	for (int i = 0; i < frames; ++i) {
		const Color c{ static_cast<unsigned char>(i * 20), 0, 0, 255 };
		ImageDrawRectangle(&sheet, i * frame_size, 0, frame_size, frame_size, c);
	}
	int size = 0;
	unsigned char *bytes = ExportImageToMemory(sheet, ".png", &size);
	UnloadImage(sheet);
	REQUIRE(bytes != nullptr);
	std::vector<std::byte> out(reinterpret_cast<std::byte *>(bytes), reinterpret_cast<std::byte *>(bytes) + size);
	MemFree(bytes);
	return out;
}

bool in_unit_range(const AtlasRect &r) {
	return r.u0 >= 0.0f && r.v0 >= 0.0f && r.u1 <= 1.0f && r.v1 <= 1.0f && r.u1 > r.u0 && r.v1 > r.v0;
}

bool same_rect(const AtlasRect &a, const AtlasRect &b) {
	return a.u0 == b.u0 && a.v0 == b.v0 && a.u1 == b.u1 && a.v1 == b.v1;
}

} // namespace

TEST_CASE("CrackAtlas::build: blocks with no override share one identical default row across stages") {
	BlockRegistry registry = BlockRegistry::base();
	const VirtualFs empty_vfs;
	const CrackAtlas atlas = CrackAtlas::build(registry, empty_vfs);

	for (int s = 0; s < CrackAtlas::kStages; ++s) {
		const AtlasRect stone_rect = atlas.rect_for(vb::world::base_block::stone, s);
		const AtlasRect dirt_rect = atlas.rect_for(vb::world::base_block::dirt, s);
		CHECK(in_unit_range(stone_rect));
		CHECK(same_rect(stone_rect, dirt_rect));
	}

	// Stage rects are distinct cells (never the same rect for two different
	// stages of the shared default row).
	const AtlasRect s0 = atlas.rect_for(vb::world::base_block::stone, 0);
	const AtlasRect s1 = atlas.rect_for(vb::world::base_block::stone, 1);
	CHECK_FALSE(same_rect(s0, s1));
}

TEST_CASE("CrackAtlas::build slices a valid crack_texture override into its own per-stage row") {
	BlockRegistry registry = BlockRegistry::base();
	registry.set_crack_texture(vb::world::base_block::stone, "textures/stone_crack.png");

	VirtualFs vfs;
	vfs["textures/stone_crack.png"] = encode_stage_sheet(CrackAtlas::kStages, 8);

	const CrackAtlas atlas = CrackAtlas::build(registry, vfs);

	// The overridden block's stage 0 differs from the shared default row --
	// it landed in its own row, not silently falling back.
	const AtlasRect default_row_stage0 = atlas.rect_for(vb::world::base_block::dirt, 0);
	const AtlasRect stone_stage0 = atlas.rect_for(vb::world::base_block::stone, 0);
	CHECK_FALSE(same_rect(stone_stage0, default_row_stage0));

	// Every stage still lands in-range and stays within the same row (same
	// v-span) as every other stage for this block.
	for (int s = 0; s < CrackAtlas::kStages; ++s) {
		const AtlasRect r = atlas.rect_for(vb::world::base_block::stone, s);
		CHECK(in_unit_range(r));
		CHECK(r.v0 == stone_stage0.v0);
	}
}

TEST_CASE("CrackAtlas::build falls back to the shared default row when a crack_texture is the wrong shape") {
	BlockRegistry registry = BlockRegistry::base();
	registry.set_crack_texture(vb::world::base_block::stone, "textures/bad_crack.png");

	VirtualFs vfs;
	// Wrong shape: width isn't kStages * height.
	vfs["textures/bad_crack.png"] = encode_stage_sheet(3, 8);

	const CrackAtlas atlas = CrackAtlas::build(registry, vfs);

	const AtlasRect stone_rect = atlas.rect_for(vb::world::base_block::stone, 0);
	const AtlasRect dirt_rect = atlas.rect_for(vb::world::base_block::dirt, 0);
	CHECK(same_rect(stone_rect, dirt_rect));
}

TEST_CASE("CrackAtlas::build falls back to the shared default row when a crack_texture path is "
		  "missing from virtual_fs") {
	BlockRegistry registry = BlockRegistry::base();
	registry.set_crack_texture(vb::world::base_block::stone, "textures/never_synced_crack.png");

	const VirtualFs empty_vfs;
	const CrackAtlas atlas = CrackAtlas::build(registry, empty_vfs); // must not crash

	const AtlasRect stone_rect = atlas.rect_for(vb::world::base_block::stone, 2);
	const AtlasRect dirt_rect = atlas.rect_for(vb::world::base_block::dirt, 2);
	CHECK(same_rect(stone_rect, dirt_rect));
}

TEST_CASE("CrackAtlas::rect_for clamps an out-of-range stage into [0, kStages)") {
	BlockRegistry registry = BlockRegistry::base();
	const VirtualFs empty_vfs;
	const CrackAtlas atlas = CrackAtlas::build(registry, empty_vfs);

	const AtlasRect last = atlas.rect_for(vb::world::base_block::stone, CrackAtlas::kStages - 1);
	const AtlasRect past_end = atlas.rect_for(vb::world::base_block::stone, CrackAtlas::kStages + 5);
	CHECK(same_rect(last, past_end));

	const AtlasRect first = atlas.rect_for(vb::world::base_block::stone, 0);
	const AtlasRect negative = atlas.rect_for(vb::world::base_block::stone, -3);
	CHECK(same_rect(first, negative));
}
