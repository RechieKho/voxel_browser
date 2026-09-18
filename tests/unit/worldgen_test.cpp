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
#include "vb/worldgen/noise_graph.hpp"
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
	CHECK(digest == 0x021BB3847413D8A5ull);

	// Same params, fresh generator -> same digest.
	WorldGenerator gen2(params, registry);
	CHECK(hash_region(gen2, region) == digest);

	// Different seed -> different digest.
	WorldGenParams other = params;
	other.seed = 1;
	CHECK(hash_region(WorldGenerator(other, registry), region) != digest);
}

namespace {

// Builds a small pack-driven pipeline directly in C++ (no Lua involved --
// PackRuntime::build_worldgen_pipeline's own parsing is covered by
// pack_runtime_integration_test.cpp's end-to-end case). Exercises height
// (fbm-over-value), one biome, one carver, and one vein, so this golden case
// actually walks every branch of WorldGenerator::generate()'s pipeline path.
std::shared_ptr<const PackWorldGenPipeline> make_test_pipeline(
		const vb::world::BlockRegistry &registry, std::uint64_t seed) {
	auto height_source = std::make_shared<NoiseNode>();
	height_source->type = NoiseNodeType::kValue;
	height_source->salt = 0;
	height_source->frequency = 1.0 / 64.0;

	auto height_fbm = std::make_shared<NoiseNode>();
	height_fbm->type = NoiseNodeType::kFbm;
	height_fbm->salt = 1;
	height_fbm->frequency = 1.0 / 64.0;
	height_fbm->octaves = 4;
	height_fbm->lacunarity = 2.0;
	height_fbm->gain = 0.5;
	height_fbm->source = height_source;

	auto carver_node = std::make_shared<NoiseNode>();
	carver_node->type = NoiseNodeType::kValue;
	carver_node->salt = 2;
	carver_node->frequency = 1.0 / 20.0;

	auto pipeline = std::make_shared<PackWorldGenPipeline>();
	pipeline->height_field = [height_fbm, seed](double x, double z) {
		const double n = height_fbm->eval2(seed, x, z);
		return 64.0 + (n * 2.0 - 1.0) * 20.0;
	};
	pipeline->sea_level = 62;
	pipeline->soil_depth = 3;

	std::vector<BiomeEntry> entries(1);
	entries[0].name = "test:plains";
	entries[0].probability = 1.0;
	entries[0].surface = registry.find("base:grass");
	entries[0].filler = registry.find("base:dirt");
	entries[0].stone = registry.find("base:stone");
	entries[0].adjacency = { 1.0 };
	pipeline->biomes = BiomeSelector(seed, 128.0, entries);
	pipeline->decoration.push_back({});

	CarverDef carver;
	carver.threshold = 0.82;
	carver.y_min = 0;
	carver.y_max = 60;
	carver.density = [carver_node, seed](double x, double y, double z) {
		return carver_node->eval3(seed, x, y, z);
	};
	pipeline->carvers.push_back(std::move(carver));

	VeinDef vein;
	vein.block = registry.find("base:sand"); // stand-in "ore" for this test
	vein.target_rock = registry.find("base:stone");
	vein.height_min = 0;
	vein.height_max = 60;
	vein.vein_size = 5;
	vein.spawn_rate = 0.5;
	pipeline->veins.push_back(vein);

	return pipeline;
}

} // namespace

TEST_CASE("worldgen determinism gate, pack-driven pipeline (golden value)") {
	auto registry = vb::world::BlockRegistry::base();
	WorldGenParams params;
	params.seed = 0x5DEECE66DULL;
	WorldGenerator gen(params, registry, make_test_pipeline(registry, params.seed));

	const std::array<ChunkCoord, 4> region{ { { 0, 1, 0 }, { 1, 1, 0 }, { 0, 1, 1 }, { -1, 2, -1 } } };
	const std::uint64_t digest = hash_region(gen, region);

	// Golden, same posture as the fixed-pipeline gate above: if this line
	// ever needs updating, the pack-driven pipeline's output changed --
	// bump pack_version and note why.
	CHECK(digest == 0x33CA94E677AF7922ull);

	WorldGenerator gen2(params, registry, make_test_pipeline(registry, params.seed));
	CHECK(hash_region(gen2, region) == digest);

	WorldGenParams other = params;
	other.seed = 1;
	CHECK(hash_region(WorldGenerator(other, registry, make_test_pipeline(registry, other.seed)),
				region) != digest);

	// And the pack-driven digest differs from the fixed default path's own
	// golden value -- the pipeline branch in generate() is actually taken,
	// not silently falling back.
	CHECK(digest != 0x021BB3847413D8A5ull);
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

// Regression: JoinGrant::spawn_pos used to default to a fixed {0, 64, 0}
// regardless of seed. With base_height=64 and amplitude=28 the real surface
// ranges roughly [36, 92], so a fixed spawn Y landed at or below the actual
// surface for a large fraction of seeds, embedding the player in solid
// terrain from the moment they joined -- no falling involved. Seed 1 is a
// dramatic case (surface_height(0,0) == 78, fourteen blocks above the old
// fixed spawn).
TEST_CASE("default_spawn_position always sits on top of the real surface") {
	auto registry = vb::world::BlockRegistry::base();
	for (const std::uint64_t seed : { std::uint64_t{ 1 }, std::uint64_t{ 7 },
				 std::uint64_t{ 19 } }) {
		WorldGenParams p;
		p.seed = seed;
		const WorldGenerator gen(p, registry);
		const int surface = gen.surface_height(0, 0);

		const vb::core::Vec3d spawn = default_spawn_position(gen);
		CHECK(spawn.x == doctest::Approx(0.5));
		CHECK(spawn.z == doctest::Approx(0.5));
		// One voxel above the topmost solid block, not inside/below it.
		CHECK(spawn.y == doctest::Approx(static_cast<double>(surface) + 1.0));
		CHECK(spawn.y > static_cast<double>(surface));
	}
}

TEST_CASE("default_spawn_position honours a non-origin spawn column") {
	auto registry = vb::world::BlockRegistry::base();
	WorldGenParams p;
	p.seed = 1;
	const WorldGenerator gen(p, registry);

	const vb::core::Vec3d spawn = default_spawn_position(gen, 40, -12);
	CHECK(spawn.x == doctest::Approx(40.5));
	CHECK(spawn.z == doctest::Approx(-11.5));
	CHECK(spawn.y ==
			doctest::Approx(static_cast<double>(gen.surface_height(40, -12)) + 1.0));
}
