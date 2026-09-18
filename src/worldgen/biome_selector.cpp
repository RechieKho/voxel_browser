#include "vb/worldgen/biome_selector.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_map>

#include "vb/core/noise.hpp"

namespace vb::worldgen {

namespace {

struct CellKey {
	std::int64_t x;
	std::int64_t y;
	bool operator==(const CellKey &) const = default;
};

struct CellKeyHash {
	std::size_t operator()(const CellKey &k) const {
		return static_cast<std::size_t>(
				core::noise::hash2(0, k.x, k.y));
	}
};

using Memo = std::unordered_map<CellKey, std::size_t, CellKeyHash>;

// hash(seed, cell) as the cell's canonical resolution-order key -- lower key
// resolves first. Distinct salt from the biome-pick RNG below so the two
// draws aren't correlated.
std::uint64_t order_key(std::uint64_t seed, std::int64_t cx, std::int64_t cy) {
	return core::noise::hash2(seed ^ 0xB10E5E1EC7100001ULL, cx, cy);
}

// Resolves `(cx, cy)`, recursing into only the (at most 8) grid-adjacent
// neighbors whose order_key sorts strictly before this cell's own -- bounded,
// terminating (order keys strictly decrease along any recursion chain), and
// memoized within this one top-level resolve() call via `memo`/`in_progress`
// (the latter guards the astronomically unlikely case of a hash tie forming
// a cycle).
std::size_t resolve_cell(std::uint64_t seed, double cell_size,
		const std::vector<BiomeEntry> &biomes, std::int64_t cx, std::int64_t cy,
		Memo &memo, std::vector<CellKey> &in_progress) {
	if (const auto it = memo.find({ cx, cy }); it != memo.end()) {
		return it->second;
	}
	if (std::find(in_progress.begin(), in_progress.end(), CellKey{ cx, cy }) !=
			in_progress.end()) {
		// Defensive only: a genuine cycle would require two neighboring
		// cells to hash to the exact same order key, which never happens in
		// practice with a 64-bit hash. Break it by picking biome 0.
		return 0;
	}
	in_progress.push_back({ cx, cy });

	const std::uint64_t my_key = order_key(seed, cx, cy);
	struct ResolvedNeighbor {
		std::size_t biome_index;
	};
	std::vector<ResolvedNeighbor> resolved_neighbors;
	resolved_neighbors.reserve(8);
	for (std::int64_t oy = -1; oy <= 1; ++oy) {
		for (std::int64_t ox = -1; ox <= 1; ++ox) {
			if (ox == 0 && oy == 0) {
				continue;
			}
			const std::int64_t nx = cx + ox;
			const std::int64_t ny = cy + oy;
			if (order_key(seed, nx, ny) < my_key) {
				resolved_neighbors.push_back(
						{ resolve_cell(seed, cell_size, biomes, nx, ny, memo, in_progress) });
			}
		}
	}

	// Weighted draw: base probability times the product of this candidate
	// biome's adjacency multiplier against every already-resolved neighbor,
	// floor-clamped so no candidate ever hits exactly 0 (spec: soft
	// multipliers, never hard exclusions).
	std::vector<double> weights(biomes.size());
	double total = 0.0;
	for (std::size_t i = 0; i < biomes.size(); ++i) {
		double w = std::max(biomes[i].probability, 0.0);
		for (const auto &n : resolved_neighbors) {
			double mult = 1.0;
			if (n.biome_index < biomes[i].adjacency.size()) {
				mult = biomes[i].adjacency[n.biome_index];
			}
			w *= std::max(mult, kAdjacencyFloor);
		}
		w = std::max(w, kAdjacencyFloor);
		weights[i] = w;
		total += w;
	}

	const double pick = core::noise::to_unit(
			core::noise::hash2(seed ^ 0xB10E5E1EC7100002ULL, cx, cy)) * total;
	double cursor = 0.0;
	std::size_t chosen = biomes.size() - 1;
	for (std::size_t i = 0; i < biomes.size(); ++i) {
		cursor += weights[i];
		if (pick < cursor) {
			chosen = i;
			break;
		}
	}

	in_progress.pop_back();
	memo.emplace(CellKey{ cx, cy }, chosen);
	return chosen;
}

} // namespace

BiomeSelector::BiomeSelector(std::uint64_t seed, double cell_size,
		std::vector<BiomeEntry> biomes)
		: seed_(seed), cell_size_(cell_size > 0.0 ? cell_size : 256.0),
		  biomes_(std::move(biomes)) {}

std::size_t BiomeSelector::resolve(double world_x, double world_z) const {
	if (biomes_.empty()) {
		return 0;
	}
	const auto cx = static_cast<std::int64_t>(
			std::floor(world_x / cell_size_));
	const auto cy = static_cast<std::int64_t>(
			std::floor(world_z / cell_size_));
	Memo memo;
	std::vector<CellKey> in_progress;
	return resolve_cell(seed_, cell_size_, biomes_, cx, cy, memo, in_progress);
}

} // namespace vb::worldgen
