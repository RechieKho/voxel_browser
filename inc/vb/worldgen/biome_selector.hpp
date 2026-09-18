#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "vb/core/ids.hpp"

// Voronoi-cell, adjacency-weighted biome selection (spec §6 stage 2; design
// finalized in STATE.md 2026-09-17). The world is partitioned into a
// jittered grid of cells (vb/core/noise.hpp's cellular2, keyed by
// (world_seed, cell_id)); each cell's biome is a weighted draw over every
// `vb.register_biome`'d biome, where the weight is that biome's base
// probability times an adjacency-compatibility multiplier against whichever
// neighbor cells resolve *before* this one in a fixed, coordinate-pure order
// (hash(seed, cell_id) -- never exploration order, so two players approaching
// the same seed from different directions see identical layouts). Adjacency
// multipliers are soft and floor-clamped (kAdjacencyFloor below) -- never a
// hard 0 -- so resolution always succeeds in one pass, no backtracking.
//
// Deliberately NOT globally memoized/locked: resolve() recomputes its (small,
// bounded -- "only a handful of neighbors ever need it" per the design note)
// neighbor recursion from scratch on every call using a purely local, stack-
// only memo. That keeps it a pure function safe to call concurrently from
// every WorldGenWorkerPool worker thread with zero shared mutable state or
// locking, at the cost of redoing cheap work across repeated nearby queries
// -- an intentional simplicity-over-cache-hit-rate trade for a first cut.

namespace vb::worldgen {

// A pack-specified adjacency multiplier of exactly 0 still gets clamped up to
// this floor when resolving weights -- "soft multipliers, never hard
// exclusions" per the design note, guaranteeing a cell can never hit a
// contradiction.
inline constexpr double kAdjacencyFloor = 1.0e-3;

struct BiomeEntry {
	std::string name;
	double probability = 1.0; // base spawn weight, pack-supplied
	core::BlockId surface = core::BlockId::kAir;
	core::BlockId filler = core::BlockId::kAir;
	core::BlockId stone = core::BlockId::kAir;
	// adjacency[j] is this biome's compatibility multiplier against the
	// biome at index j (indices match BiomeSelector's own biome list order).
	// Missing/unspecified pairs default to 1.0 (neutral).
	std::vector<double> adjacency;
};

class BiomeSelector {
public:
	BiomeSelector() = default;
	BiomeSelector(std::uint64_t seed, double cell_size, std::vector<BiomeEntry> biomes);

	bool empty() const { return biomes_.empty(); }
	std::size_t biome_count() const { return biomes_.size(); }
	const BiomeEntry &biome(std::size_t index) const { return biomes_[index]; }

	// Resolves the biome index owning the Voronoi cell containing world
	// column (world_x, world_z). Pure function of (seed_, cell_size_,
	// biomes_, world_x, world_z) -- safe to call from any thread.
	std::size_t resolve(double world_x, double world_z) const;

private:
	std::uint64_t seed_ = 0;
	double cell_size_ = 256.0;
	std::vector<BiomeEntry> biomes_;
};

} // namespace vb::worldgen
