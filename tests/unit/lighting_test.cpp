#include <doctest/doctest.h>

#include <memory>
#include <ostream>
#include <unordered_map>
#include <vector>

#include "vb/world/block.hpp"
#include "vb/world/chunk.hpp"
#include "vb/world/lighting.hpp"
#include "vb/world/paletted_chunk_store.hpp"

using namespace vb::world;
using vb::core::BlockId;
using vb::core::ChunkCoord;

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

// --- cross-chunk sky light (the "bright band every 32 blocks" bug) ------

TEST_CASE("relight_chunk with no `above` still assumes open sky (unchanged "
		"default behaviour)") {
	Chunk c({ 0, 0, 0 });
	LightEngine(BlockRegistry::base()).relight_chunk(c, nullptr);
	CHECK(c.light(5, kChunkDim - 1, 5).sky() == kMaxLight);
}

TEST_CASE("relight_chunk with a solid `above` is dark at the top, not "
		"falsely sky-lit") {
	auto reg = BlockRegistry::base();
	Chunk above({ 0, 1, 0 });
	above.blocks().fill(base_block::stone); // completely seals the sky
	LightEngine(reg).relight_chunk(above); // above == nullptr: it IS the top

	Chunk below({ 0, 0, 0 }); // open air throughout -- would read full-bright
	LightEngine(reg).relight_chunk(below, &above); // if `above` were ignored

	CHECK(below.light(5, kChunkDim - 1, 5).sky() == 0);
	CHECK(below.light(5, 0, 5).sky() == 0);
}

TEST_CASE("relight_chunk with an open `above` propagates full brightness "
		"straight down, no falloff") {
	auto reg = BlockRegistry::base();
	Chunk above({ 0, 1, 0 }); // empty: fully sky-lit itself
	LightEngine(reg).relight_chunk(above);

	Chunk below({ 0, 0, 0 });
	LightEngine(reg).relight_chunk(below, &above);

	CHECK(below.light(5, kChunkDim - 1, 5).sky() == kMaxLight);
	CHECK(below.light(5, 0, 5).sky() == kMaxLight); // straight down, no falloff
}

namespace {

// A minimal in-memory chunk store for exercising relight_column() the same
// way World / ClientChunkStore do: a coord -> Chunk map plus a find()
// closure, with no other machinery.
struct FakeStore {
	std::unordered_map<ChunkCoord, std::unique_ptr<Chunk>> chunks;

	Chunk *find(ChunkCoord c) {
		const auto it = chunks.find(c);
		return it == chunks.end() ? nullptr : it->second.get();
	}
	Chunk &put(ChunkCoord c) {
		auto chunk = std::make_unique<Chunk>(c);
		Chunk &ref = *chunk;
		chunks[c] = std::move(chunk);
		return ref;
	}
};

} // namespace

TEST_CASE("relight_column cascades a solid roof's shadow down through "
		"every loaded chunk below it") {
	// Regression for the reported bug: mining down, every 32 blocks (a chunk
	// boundary) the world was briefly/falsely bright, because each chunk was
	// always relit in isolation, assuming open sky at its own top layer
	// regardless of what was actually above it.
	FakeStore store;
	auto find = [&](ChunkCoord c) { return store.find(c); };
	LightEngine engine(BlockRegistry::base());

	Chunk &top = store.put({ 0, 1, 0 });
	top.blocks().fill(base_block::stone); // seals the sky completely
	Chunk &mid = store.put({ 0, 0, 0 }); // open air
	Chunk &bottom = store.put({ 0, -1, 0 }); // open air

	std::vector<ChunkCoord> relit;
	relight_column(
			engine, ChunkCoord{ 0, 1, 0 }, find,
			[&](ChunkCoord c, const std::array<Light, kChunkVolume> &, const Chunk &) {
				relit.push_back(c);
			});

	// The shadow reaches all the way down: every loaded chunk in the column
	// is dark, not just the directly-below one.
	CHECK(mid.light(5, kChunkDim - 1, 5).sky() == 0);
	CHECK(bottom.light(5, kChunkDim - 1, 5).sky() == 0);
	CHECK(relit == std::vector<ChunkCoord>{ { 0, 1, 0 }, { 0, 0, 0 }, { 0, -1, 0 } });
}

TEST_CASE("relight_column visits every loaded chunk down to the bottom of "
		"the stack even when nothing changes, but only bumps revisions for "
		"chunks whose light output actually changed") {
	FakeStore store;
	auto find = [&](ChunkCoord c) { return store.find(c); };
	LightEngine engine(BlockRegistry::base());

	// Both chunks start open (fully sky-lit) and already correctly lit.
	Chunk &top = store.put({ 0, 1, 0 });
	engine.relight_chunk(top);
	Chunk &below = store.put({ 0, 0, 0 });
	engine.relight_chunk(below, &top);
	const std::uint64_t below_rev_before = below.revision();

	// Relighting `top` again (nothing about it changed) still visits `below`
	// (unconditional cascade -- see the header comment for why an early-exit
	// on "looks unchanged" is unsound), but since below's recomputed output
	// really is byte-identical, its revision must not bump.
	std::vector<ChunkCoord> relit;
	relight_column(
			engine, ChunkCoord{ 0, 1, 0 }, find,
			[&](ChunkCoord c, const std::array<Light, kChunkVolume> &, const Chunk &) {
				relit.push_back(c);
			});

	CHECK(relit == std::vector<ChunkCoord>{ { 0, 1, 0 }, { 0, 0, 0 } });
	CHECK(below.revision() == below_rev_before);
}

TEST_CASE("relight_column always reports the starting chunk even if its "
		"light happens not to change") {
	// A caller with an unrelated (e.g. block-only) change to report for the
	// starting chunk still needs exactly one callback to hang it on, even in
	// the (rare) case relighting doesn't change any light byte.
	FakeStore store;
	auto find = [&](ChunkCoord c) { return store.find(c); };
	LightEngine engine(BlockRegistry::base());

	Chunk &c = store.put({ 0, 0, 0 });
	c.blocks().fill(base_block::stone); // fully opaque: sky light stays 0 throughout

	int calls = 0;
	relight_column(engine, ChunkCoord{ 0, 0, 0 }, find,
			[&](ChunkCoord, const std::array<Light, kChunkVolume> &, const Chunk &) {
				++calls;
			});
	CHECK(calls == 1);
}
