#include <doctest/doctest.h>

#include <ostream>

#include <random>

#include "vb/world/block.hpp"
#include "vb/world/chunk.hpp"
#include "vb/world/paletted_chunk_store.hpp"
#include "vb/world/world.hpp"

using namespace vb::world;
using vb::core::BlockId;
using vb::core::IVec3;

namespace {
BlockId b(std::uint16_t v) { return static_cast<BlockId>(v); }
} // namespace

TEST_CASE("PalettedChunkStore: homogeneous by default, zero index storage") {
	PalettedChunkStore s(BlockId::kAir);
	CHECK(s.is_homogeneous());
	CHECK(s.packed_bytes() == 0);
	CHECK(s.get(0) == BlockId::kAir);
	CHECK(s.get(31, 31, 31) == BlockId::kAir);
}

TEST_CASE("PalettedChunkStore: single set grows to 1 bit and round-trips") {
	PalettedChunkStore s(BlockId::kAir);
	CHECK(s.set(1, 2, 3, b(7)));
	CHECK_FALSE(s.set(1, 2, 3, b(7))); // no-op
	CHECK(s.bits_per_index() == 1);
	CHECK(s.get(1, 2, 3) == b(7));
	CHECK(s.get(0, 0, 0) == BlockId::kAir);
}

TEST_CASE("PalettedChunkStore: index width grows with the palette") {
	PalettedChunkStore s(BlockId::kAir);
	for (std::uint16_t i = 1; i <= 20; ++i) {
		s.set(static_cast<std::size_t>(i), b(i));
	}
	CHECK(s.bits_per_index() == 8); // 21 palette entries -> 8 bits
	for (std::uint16_t i = 1; i <= 20; ++i) {
		CHECK(s.get(static_cast<std::size_t>(i)) == b(i));
	}
}

TEST_CASE("PalettedChunkStore: randomized round-trip survives repacking") {
	PalettedChunkStore s(BlockId::kAir);
	std::mt19937 rng(12345);
	std::vector<BlockId> shadow(kChunkVolume, BlockId::kAir);

	for (int iter = 0; iter < 4000; ++iter) {
		const std::size_t idx = rng() % kChunkVolume;
		const auto block = b(static_cast<std::uint16_t>(rng() % 300));
		s.set(idx, block);
		shadow[idx] = block;
	}
	for (std::size_t i = 0; i < kChunkVolume; ++i) {
		REQUIRE(s.get(i) == shadow[i]);
	}
	CHECK(s.bits_per_index() == 16);
}

TEST_CASE("PalettedChunkStore: fill collapses back to homogeneous") {
	PalettedChunkStore s(BlockId::kAir);
	s.set(5, b(9));
	s.fill(b(3));
	CHECK(s.is_homogeneous());
	CHECK(s.get(5) == b(3));
	CHECK(s.get(999) == b(3));
}

TEST_CASE("PalettedChunkStore: compact collapses a genuinely uniform chunk") {
	PalettedChunkStore s(BlockId::kAir);
	for (std::size_t i = 0; i < kChunkVolume; ++i) {
		s.set(i, b(2));
	}
	CHECK_FALSE(s.is_homogeneous());
	s.compact();
	CHECK(s.is_homogeneous());
	CHECK(s.get(0) == b(2));
	CHECK(s.get(kChunkVolume - 1) == b(2));
}

TEST_CASE("PalettedChunkStore: compact drops dead palette entries") {
	PalettedChunkStore t(BlockId::kAir);
	t.set(0, b(1));
	t.set(1, b(5));
	t.set(1, b(1)); // b(5) dead
	t.compact();
	CHECK(t.palette().size() == 2); // air + b(1)
	CHECK(t.get(0) == b(1));
	CHECK(t.get(1) == b(1));
	CHECK(t.get(2) == BlockId::kAir);
}

TEST_CASE("Chunk: edits bump revision and set dirty flags") {
	Chunk c({ 0, 0, 0 });
	CHECK(c.revision() == 0);
	CHECK_FALSE(c.dirty().any());
	c.set(1, 1, 1, b(2));
	CHECK(c.revision() == 1);
	CHECK(c.dirty().mesh);
	CHECK(c.dirty().light);
	c.set(1, 1, 1, b(2)); // no change
	CHECK(c.revision() == 1);
}

TEST_CASE("Chunk light byte packs sky and block nibbles") {
	Light l;
	l.set_sky(15);
	l.set_block(4);
	CHECK(l.sky() == 15);
	CHECK(l.block() == 4);
	CHECK(l.max() == 15);
}

TEST_CASE("World: block access crosses chunk boundaries with negative coords") {
	World w(BlockRegistry::base());
	CHECK(w.get_block({ -1, 5, 40 }) == BlockId::kAir);

	CHECK(w.set_block({ -1, 5, 40 }, base_block::stone));
	CHECK(w.get_block({ -1, 5, 40 }) == base_block::stone);
	CHECK(w.chunk_count() == 1);

	const auto a = address_of({ -1, 5, 40 });
	CHECK(a.chunk == vb::core::ChunkCoord{ -1, 0, 1 });
	CHECK(a.lx == 31);
	CHECK(a.lz == 8);

	CHECK(w.solid_at({ -1, 5, 40 }));
	CHECK_FALSE(w.solid_at({ 0, 0, 0 }));

	CHECK(w.unload_chunk(a.chunk));
	CHECK(w.chunk_count() == 0);
	CHECK(w.get_block({ -1, 5, 40 }) == BlockId::kAir);
}

TEST_CASE("BlockRegistry base set") {
	const auto r = BlockRegistry::base();
	CHECK(r.size() == 8);
	CHECK(r.find("base:grass") == base_block::grass);
	CHECK(r.is_solid(base_block::stone));
	CHECK_FALSE(r.is_solid(base_block::water));
	CHECK(r.is_liquid(base_block::water));
	CHECK_FALSE(r.is_opaque(base_block::leaves));
	CHECK(r.find("base:nonesuch") == BlockId::kAir);
}
