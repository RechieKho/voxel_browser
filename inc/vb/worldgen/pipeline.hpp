#pragma once

#include <functional>
#include <vector>

#include "vb/core/ids.hpp"
#include "vb/core/math.hpp"
#include "vb/worldgen/biome_selector.hpp"
#include "vb/worldgen/structure.hpp"

// The fully-parsed, immutable result of a pack's `vb.worldgen.set_pipeline`
// call plus every `vb.register_biome` entry (Phase 6.14). Built exactly once
// on the main thread by PackRuntime::build_worldgen_pipeline
// (src/script/pack_runtime.cpp) -- no Lua/sol2 references anywhere in this
// type, so it's safe to hand a `std::shared_ptr<const PackWorldGenPipeline>`
// to WorldGenWorkerPool's worker threads and evaluate it lock-free.
//
// `height_field`/carver density functions are `std::function` rather than a
// `worldgen::NoiseNode` directly so the *evaluator* backend (the always-
// available hand-rolled vb/core/noise.hpp one, or -- when VB_WITH_WORLDGEN is
// on -- a real FastNoise2 SmartNode graph) stays an implementation detail
// private to src/script/pack_runtime.cpp's build step (see
// src/worldgen/noise_graph.cpp's header comment); this header never needs to
// know which one is in use.

namespace vb::worldgen {

struct CarverDef {
	// >= threshold at a given (x,y,z) carves that voxel to air. y_min/y_max
	// bound where this carver is even sampled (cheap early-out).
	std::function<double(double x, double y, double z)> density;
	double threshold = 0.5;
	int y_min = 0;
	int y_max = 255;
};

struct VeinDef {
	core::BlockId block = core::BlockId::kAir;
	core::BlockId target_rock = core::BlockId::kAir; // only replaces this block
	int height_min = 0;
	int height_max = 63;
	int vein_size = 6; // blocks per vein cluster
	double spawn_rate = 0.02; // expected veins per chunk column
};

struct PackWorldGenPipeline {
	std::function<double(double world_x, double world_z)> height_field;
	int sea_level = 62;
	int soil_depth = 4;
	// Optional: columns whose surface is at or below sea_level + 1 get this
	// block for both surface and filler (the fixed default path's sand
	// beaches). kAir = off, every biome keeps its own surface down to the
	// sea floor.
	core::BlockId beach = core::BlockId::kAir;

	BiomeSelector biomes;
	std::vector<CarverDef> carvers;
	std::vector<VeinDef> veins;
	// Every registered structure (vb.register_structure order), followed by
	// the anonymous structures built from inline `{blocks = ...}` decoration
	// entries. PlacementRule::structure indexes this.
	std::vector<StructureDef> structures;
	// Indexed by biome index (matches `biomes`'s own order); may be shorter
	// than biome_count() if trailing biomes have no decoration entries.
	std::vector<std::vector<PlacementRule>> decoration;
	// Indexed by BlockId: 1 for blocks flagged `replaceable` (leaves, tall
	// grass...), read by `replace = "air_and_plants"`. Copied from the
	// registry at build time so worker threads never touch it.
	std::vector<std::uint8_t> replaceable;

	const std::vector<PlacementRule> &decoration_for(std::size_t biome_index) const {
		static const std::vector<PlacementRule> kEmpty;
		return biome_index < decoration.size() ? decoration[biome_index] : kEmpty;
	}

	bool is_replaceable(core::BlockId id) const {
		const auto idx = static_cast<std::size_t>(id);
		return idx < replaceable.size() && replaceable[idx] != 0;
	}
};

} // namespace vb::worldgen
