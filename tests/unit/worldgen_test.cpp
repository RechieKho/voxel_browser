#include <doctest/doctest.h>

#include <ostream>

#include <array>
#include <cstdint>
#include <span>
#include <thread>

#include "vb/core/hash.hpp"
#include "vb/core/ids.hpp"
#include "vb/core/noise.hpp"
#include "vb/world/block.hpp"
#include "vb/world/chunk.hpp"
#include "vb/world/paletted_chunk_store.hpp"
#include "vb/worldgen/generator.hpp"
#include "vb/worldgen/worker_pool.hpp"

using namespace vb::worldgen;
using vb::core::ChunkCoord;

namespace {

std::uint64_t hash_region(const WorldGenerator &gen,
		std::span<const ChunkCoord> coords) {
	vb::core::Fnv1a h;
	for (ChunkCoord c : coords) {
		vb::world::Chunk chunk(c);
		gen.generate(chunk);
		h.update_u<std::uint32_t>(static_cast<std::uint32_t>(c.x));
		h.update_u<std::uint32_t>(static_cast<std::uint32_t>(c.y));
		h.update_u<std::uint32_t>(static_cast<std::uint32_t>(c.z));
		for (std::size_t i = 0; i < vb::world::kChunkVolume; ++i) {
			h.update_u<std::uint16_t>(
					static_cast<std::uint16_t>(chunk.blocks().get(i)));
		}
	}
	return h.digest();
}

} // namespace

TEST_CASE("noise is in range and reproducible") {
	const vb::core::noise::FbmParams p{ 4, 0.01, 2.0, 0.5 };
	for (int i = 0; i < 200; ++i) {
		const double x = i * 3.7;
		const double v = vb::core::noise::fbm2(99, x, -x, p);
		CHECK(v >= 0.0);
		CHECK(v < 1.0);
		CHECK(v == vb::core::noise::fbm2(99, x, -x, p)); // pure
	}
	// Different seeds diverge.
	CHECK(vb::core::noise::value2(1, 2.5, 3.5) !=
			vb::core::noise::value2(2, 2.5, 3.5));
}

TEST_CASE("generator produces layered terrain around sea level") {
	auto registry = vb::world::BlockRegistry::base();
	WorldGenParams params;
	params.seed = 4242;
	WorldGenerator gen(params, registry);

	vb::world::Chunk chunk({ 0, 2, 0 }); // world y 64..95
	gen.generate(chunk);
	CHECK(chunk.gen_state() == vb::world::GenState::kGenerated);
	CHECK(chunk.dirty().mesh);

	// Column (0,0): the surface voxel is solid; well above it is air.
	const int h = gen.surface_height(0, 0);
	if (h >= 65 && h < 92) {
		const int ly = h - 64;
		CHECK(chunk.get(0, ly, 0) != vb::core::BlockId::kAir);
		const bool above_is_open =
				chunk.get(0, ly + 3, 0) == vb::core::BlockId::kAir ||
				chunk.get(0, ly + 3, 0) == vb::world::base_block::water;
		CHECK(above_is_open);
	}

	// Stone under, non-stone at the surface.
	vb::world::Chunk deep({ 0, 0, 0 }); // world y 0..31 -> well below terrain
	gen.generate(deep);
	CHECK(deep.blocks().uniform_value(nullptr));
	CHECK(deep.get(5, 5, 5) == vb::world::base_block::stone);
}

TEST_CASE("worldgen determinism gate (cross-platform golden value)") {
	auto registry = vb::world::BlockRegistry::base();
	WorldGenParams params;
	params.seed = 0x5DEECE66DULL;
	WorldGenerator gen(params, registry);

	const std::array<ChunkCoord, 4> region{ { { 0, 1, 0 }, { 1, 1, 0 }, { 0, 1, 1 }, { -1, 2, -1 } } };

	const std::uint64_t digest = hash_region(gen, region);

	// Golden: regenerate + re-hash must be identical. If this line ever needs
	// updating, the worldgen output changed — bump pack_version and note why.
	CHECK(digest == 0xA194330E041D3330ull);

	// Same params, fresh generator -> same digest.
	WorldGenerator gen2(params, registry);
	CHECK(hash_region(gen2, region) == digest);

	// Different seed -> different digest.
	WorldGenParams other = params;
	other.seed = 1;
	CHECK(hash_region(WorldGenerator(other, registry), region) != digest);
}

TEST_CASE("worker pool generates submitted chunks, dedupes, drains") {
	auto registry = vb::world::BlockRegistry::base();
	WorldGenWorkerPool pool(WorldGenerator(WorldGenParams{}, registry), 2);

	CHECK(pool.submit({ 0, 0, 0 }));
	CHECK(pool.submit({ 1, 0, 0 }));
	CHECK_FALSE(pool.submit({ 0, 0, 0 })); // duplicate

	std::vector<std::unique_ptr<vb::world::Chunk>> done;
	for (int spin = 0; spin < 100000 && done.size() < 2; ++spin) {
		for (auto &c : pool.poll_completed()) {
			done.push_back(std::move(c));
		}
		std::this_thread::yield();
	}
	REQUIRE(done.size() == 2);
	CHECK(done[0]->gen_state() == vb::world::GenState::kGenerated);
}
