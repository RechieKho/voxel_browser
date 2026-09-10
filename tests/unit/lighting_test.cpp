#include <doctest/doctest.h>

#include <ostream>

#include "vb/world/block.hpp"
#include "vb/world/chunk.hpp"
#include "vb/world/lighting.hpp"
#include "vb/world/paletted_chunk_store.hpp"

using namespace vb::world;
using vb::core::BlockId;

TEST_CASE("empty chunk is fully sky-lit, no block light") {
	Chunk c({ 0, 0, 0 });
	LightEngine(BlockRegistry::base()).relight_chunk(c);

	CHECK(c.light(0, kChunkDim - 1, 0).sky() == kMaxLight);
	CHECK(c.light(15, 0, 15).sky() == kMaxLight); // straight down, no falloff
	CHECK(c.light(8, 8, 8).block() == 0);
	CHECK_FALSE(c.dirty().light);
}

TEST_CASE("a solid floor blocks sky light below it, shadow attenuates sideways") {
	auto reg = BlockRegistry::base();
	Chunk c({ 0, 0, 0 });
	// Fill y=10 with stone across the whole layer.
	for (int z = 0; z < kChunkDim; ++z) {
		for (int x = 0; x < kChunkDim; ++x) {
			c.blocks().set(x, 10, z, base_block::stone);
		}
	}
	LightEngine(reg).relight_chunk(c);

	CHECK(c.light(5, 11, 5).sky() == kMaxLight); // above the slab
	CHECK(c.light(5, 9, 5).sky() == 0); // directly under, sealed
}

TEST_CASE("sky light spills under an overhang and falls off by 1 per step") {
	auto reg = BlockRegistry::base();
	Chunk c({ 0, 0, 0 });
	// A stone ceiling at y=10 covering x in [0..15], open for x in [16..31].
	for (int z = 0; z < kChunkDim; ++z) {
		for (int x = 0; x < 16; ++x) {
			c.blocks().set(x, 10, z, base_block::stone);
		}
	}
	LightEngine(reg).relight_chunk(c);

	// Just inside the covered region, one step from the open edge.
	CHECK(c.light(16, 9, 5).sky() == kMaxLight); // open column
	CHECK(c.light(15, 9, 5).sky() == kMaxLight - 1);
	CHECK(c.light(14, 9, 5).sky() == kMaxLight - 2);
	CHECK(c.light(10, 9, 5).sky() < kMaxLight - 2);
}

TEST_CASE("an emitting block floods block light radially") {
	auto reg = BlockRegistry::base();
	const BlockId lamp = reg.add({ "test:lamp", true, true, false, 14 });

	Chunk c({ 0, 0, 0 });
	c.blocks().set(16, 16, 16, lamp);
	LightEngine(reg).relight_chunk(c);

	CHECK(c.light(16, 16, 16).block() == 14);
	CHECK(c.light(17, 16, 16).block() == 13);
	CHECK(c.light(16, 16, 19).block() == 11); // 3 steps
	CHECK(c.light(16, 16, 30).block() == 0); // out of range
}

TEST_CASE("water dims but does not stop light") {
	auto reg = BlockRegistry::base();
	Chunk c({ 0, 0, 0 });
	for (int z = 0; z < kChunkDim; ++z) {
		for (int x = 0; x < kChunkDim; ++x) {
			c.blocks().set(x, 12, z, base_block::water);
		}
	}
	LightEngine(reg).relight_chunk(c);
	CHECK(c.light(5, 11, 5).sky() > 0);
	CHECK(c.light(5, 11, 5).sky() < kMaxLight);
}
