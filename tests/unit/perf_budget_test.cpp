#include <doctest/doctest.h>

#include <ostream>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cmath>
#include <memory>
#include <vector>

#include "vb/protocol/snapshot.hpp"
#include "vb/protocol/world.hpp"
#include "vb/world/block.hpp"
#include "vb/world/chunk.hpp"
#include "vb/world/chunk_codec.hpp"
#include "vb/world/chunk_mesher.hpp"
#include "vb/world/client_chunk_store.hpp"
#include "vb/world/lighting.hpp"
#include "vb/worldgen/generator.hpp"
#include "vb/worldgen/structure.hpp"

using namespace vb::world;
using vb::core::ChunkCoord;
using vb::core::EntityKindId;
using vb::core::NetId;
using vb::core::Vec2f;
using vb::core::Vec3d;
using vb::core::Vec3f;
namespace wg = vb::worldgen;

// REMAINING_TASKS.md's Cross-Cutting "Perf budget checks" item names three
// things: chunk mesh time, snapshot size, frame time. This covers the first
// two directly. Frame time (real rasterization/GPU cost) is deliberately
// out of scope here -- it needs a live GL context to mean anything, and
// this agent environment has no GUI, the same already-documented caveat
// every other rendering-adjacent item in this codebase carries. What "frame
// time" *can* mean without a GPU -- how much CPU-side work a frame's worth
// of chunk meshing/snapshot encoding costs -- is exactly what the two
// checks below are.
//
// These are budget *gates*, not a benchmarking dashboard: no historical
// tracking, no graphs, nothing scheduled separately from the normal
// vb_tests run (matching this codebase's "simple benchmark harness"
// framing, not a heavier one). Each budget is set with deliberate headroom
// above a measured baseline in this environment's unoptimized Debug build
// -- the point is catching a real regression that blows past a generous
// ceiling, not pinning today's exact numbers.
namespace {

void put(ClientChunkStore &store, const Chunk &chunk) {
	vb::protocol::S2CChunkAdd add;
	add.coord = chunk.coord();
	add.revision = 1;
	add.payload = encode_chunk_payload(chunk);
	REQUIRE(store.apply_add(add));
}

vb::protocol::EntityRecord make_entity(std::uint32_t i) {
	return { NetId{ i }, EntityKindId{ 1 },
		Vec3d{ static_cast<double>(i), 64.0, 0.0 },
		Vec2f{ 90.0f, 0.0f }, Vec3f{ 1.0f, 0.0f, 0.0f },
		/*flags*/ 1, std::nullopt, std::nullopt };
}

} // namespace

TEST_CASE("chunk mesh time for a representative surface chunk stays under "
		  "a generous budget") {
	const wg::WorldGenerator gen(wg::WorldGenParams{}, BlockRegistry::base());
	// A fixed chunk y (e.g. "wherever base_height nominally sits") isn't
	// reliable -- the real surface for any given column can dip low enough
	// to put a fixed guess entirely above open sky (an empty chunk, no
	// mesh complexity at all -- caught the hard way: an earlier version of
	// this test hardcoded y=2 and hit exactly that for this seed/column,
	// failing REQUIRE_FALSE(mesh.empty()) below). Ask the real generator
	// where its surface actually is at this chunk's center column instead.
	const int surface_y = gen.surface_height(16, 16);
	const ChunkCoord coord{ 0, surface_y / vb::core::kChunkDim, 0 };
	Chunk chunk(coord);
	gen.generate(chunk);
	LightEngine(BlockRegistry::base()).relight_chunk(chunk);

	ClientChunkStore store(BlockRegistry::base());
	put(store, chunk);

	// mesh_chunk() is a pure read of `store` -- safe to call repeatedly and
	// take the fastest run, the same "discard warmup/scheduling noise" shape
	// a micro-benchmark would use, without pulling in a benchmarking library
	// for a 3-sample budget gate.
	std::chrono::steady_clock::duration best = std::chrono::steady_clock::duration::max();
	MeshData mesh;
	for (int i = 0; i < 3; ++i) {
		const auto start = std::chrono::steady_clock::now();
		mesh = mesh_chunk(store, coord);
		best = std::min(best, std::chrono::steady_clock::now() - start);
	}

	// A real surface chunk should never mesh to nothing -- if it does, the
	// budget check below would trivially "pass" for the wrong reason.
	REQUIRE_FALSE(mesh.empty());

	const auto best_ms =
			std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(best)
					.count();
	// Generous: measured ~21ms in this environment's unoptimized Debug
	// build for a single 32^3 chunk; 100ms leaves real headroom for a
	// slower machine or a Debug-build variance day while still catching a
	// real algorithmic regression (e.g. an accidental O(n^2) pass over the
	// chunk).
	CHECK(best_ms < 100.0);
}

TEST_CASE("generating a decoration-heavy chunk stays under a generous budget "
		  "(structure pull-stamping, docs/structure-editor.md section E)") {
	const BlockRegistry registry = BlockRegistry::base();
	auto pipeline = std::make_shared<wg::PackWorldGenPipeline>();
	pipeline->height_field = [](double x, double z) {
		return 66.0 + std::sin(x * 0.05) * 4.0 + std::cos(z * 0.07) * 4.0;
	};
	pipeline->sea_level = 62;
	std::vector<wg::BiomeEntry> entries(1);
	entries[0].name = "perf:plains";
	entries[0].surface = registry.find("base:grass");
	entries[0].filler = registry.find("base:dirt");
	entries[0].stone = registry.find("base:stone");
	entries[0].adjacency = { 1.0 };
	pipeline->biomes = wg::BiomeSelector(1, 128.0, entries);
	pipeline->replaceable.assign(registry.size(), 0);

	// Three dense rules on a 9x9x9 structure: the worst case for the nine-
	// column search is many large overlapping anchors per chunk.
	wg::StructureDef big;
	big.name = "perf:big";
	big.anchor = { 4, 0, 4 };
	big.radius_xz = 4;
	wg::StructureVariant v;
	v.size = { 9, 9, 9 };
	v.cells.assign(9 * 9 * 9, registry.find("base:leaves"));
	big.variants.push_back(std::move(v));
	pipeline->structures.push_back(std::move(big));
	pipeline->decoration.assign(1, {});
	for (int i = 0; i < 3; ++i) {
		wg::PlacementRule rule;
		rule.structure = 0;
		rule.spawn_rate = 16.0;
		rule.min_spacing = 2 + i;
		rule.rotate = true;
		rule.mirror = true;
		rule.cluster = 0.5;
		rule.max_slope = 6;
		pipeline->decoration[0].push_back(rule);
	}

	wg::WorldGenParams params;
	params.seed = 42;
	const wg::WorldGenerator gen(params, registry, pipeline);
	const ChunkCoord coord{ 0, 2, 0 };

	std::chrono::steady_clock::duration best = std::chrono::steady_clock::duration::max();
	for (int i = 0; i < 3; ++i) {
		Chunk chunk(coord);
		const auto start = std::chrono::steady_clock::now();
		gen.generate(chunk);
		best = std::min(best, std::chrono::steady_clock::now() - start);
	}
	const auto best_ms =
			std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(best)
					.count();
	const auto placements = gen.structure_placements(-4, -4, 35, 35);
	REQUIRE(placements.size() > 20); // the budget is only meaningful with real work
	INFO("decoration-heavy chunk generate: ", best_ms, " ms, ", placements.size(), " anchors");
	CHECK(best_ms < 400.0);
}

TEST_CASE("S2CEntitySnapshot for a saturated interest set stays under a "
		  "per-tick byte budget") {
	// 50 entities is a deliberately busy scene -- more than any single
	// player's view distance would realistically ever surface at once
	// (spec's own InterestGrid cell radius keeps this well below in
	// practice) -- so this is a worst-case-shaped snapshot, not a typical
	// one.
	constexpr std::uint32_t kEntityCount = 50;

	vb::protocol::S2CEntitySnapshot s;
	s.server_tick = 123456;
	s.last_acked_input_seq = 999;
	for (std::uint32_t i = 0; i < kEntityCount; ++i) {
		s.updated.push_back(make_entity(i));
	}
	s.has_local = true;
	s.local = make_entity(kEntityCount);

	std::vector<std::byte> encoded;
	s.encode(encoded);

	// Generous: measured ~52 bytes/entity for this shape (no
	// visual_override set, the common case) -- 90 bytes/entity plus a
	// fixed 256-byte allowance leaves real headroom for future growth
	// (e.g. more EntityRecord fields) while still catching a real
	// regression, such as a change that starts sending visual_override on
	// every record instead of only on `entered` ones (spec's own "absent
	// entirely costs one bool on the wire" invariant, see snapshot.hpp).
	const std::size_t budget = kEntityCount * 90 + 256;
	CHECK(encoded.size() < budget);
}
