#include <doctest/doctest.h>

#include <vector>

#include "vb/worldgen/biome_selector.hpp"

// Pure C++ tests for the Phase 6.14 Voronoi-cell, adjacency-weighted biome
// selector (design finalized in STATE.md 2026-09-17). No Lua involved --
// PackRuntime::build_worldgen_pipeline (src/script/pack_runtime.cpp) is what
// turns vb.register_biome tables into BiomeEntry, covered separately by
// pack_runtime_integration_test.cpp's end-to-end case.

using vb::worldgen::BiomeEntry;
using vb::worldgen::BiomeSelector;

TEST_CASE("resolve() is a pure function, independent of query order") {
	std::vector<BiomeEntry> biomes(3);
	biomes[0].name = "a";
	biomes[1].name = "b";
	biomes[2].name = "c";
	BiomeSelector sel(123, 32.0, biomes);

	std::vector<std::pair<double, double>> points;
	for (int i = 0; i < 20; ++i) {
		points.push_back({ static_cast<double>(i) * 17.0, static_cast<double>(i) * -9.0 });
	}

	std::vector<std::size_t> forward;
	for (const auto &p : points) {
		forward.push_back(sel.resolve(p.first, p.second));
	}
	std::vector<std::size_t> backward(points.size());
	for (std::size_t i = points.size(); i-- > 0;) {
		backward[i] = sel.resolve(points[i].first, points[i].second);
	}
	CHECK(forward == backward);

	// And a second, independently-constructed selector with identical
	// parameters reproduces the exact same layout (determinism from (seed,
	// cell_size, biomes) alone, not from any cross-call cache).
	BiomeSelector sel2(123, 32.0, biomes);
	for (std::size_t i = 0; i < points.size(); ++i) {
		CHECK(sel2.resolve(points[i].first, points[i].second) == forward[i]);
	}
}

TEST_CASE("a much higher base probability dominates the weighted draw") {
	std::vector<BiomeEntry> biomes(2);
	biomes[0].name = "common";
	biomes[0].probability = 1000.0;
	biomes[1].name = "rare";
	biomes[1].probability = 1.0;
	BiomeSelector sel(7, 24.0, biomes);

	int common_count = 0;
	int total = 0;
	for (int cx = 0; cx < 30; ++cx) {
		for (int cz = 0; cz < 30; ++cz) {
			const double wx = static_cast<double>(cx) * 24.0 + 12.0;
			const double wz = static_cast<double>(cz) * 24.0 + 12.0;
			if (sel.resolve(wx, wz) == 0) {
				++common_count;
			}
			++total;
		}
	}
	CHECK(common_count > total * 8 / 10);
}

TEST_CASE("a zero-weight adjacency multiplier is floor-clamped, never a hard "
		"exclusion") {
	std::vector<BiomeEntry> biomes(2);
	biomes[0].name = "a";
	biomes[0].probability = 1.0;
	biomes[1].name = "b";
	biomes[1].probability = 1.0;
	// biome 1 ("b") is maximally averse to being adjacent to biome 0 ("a") --
	// spec: soft multiplier, never a hard 0.
	biomes[1].adjacency = { 0.0, 1.0 };
	BiomeSelector sel(99, 16.0, biomes);

	bool saw_b = false;
	for (int cx = 0; cx < 40 && !saw_b; ++cx) {
		for (int cz = 0; cz < 40 && !saw_b; ++cz) {
			const double wx = static_cast<double>(cx) * 16.0 + 8.0;
			const double wz = static_cast<double>(cz) * 16.0 + 8.0;
			if (sel.resolve(wx, wz) == 1) {
				saw_b = true;
			}
		}
	}
	CHECK(saw_b);
}

TEST_CASE("an empty selector never crashes and reports empty()") {
	BiomeSelector sel;
	CHECK(sel.empty());
	CHECK(sel.biome_count() == 0);
	CHECK(sel.resolve(0.0, 0.0) == 0); // meaningless but safe -- callers must
										// check empty() before biome(index)
}
