#pragma once

#include <functional>
#include <vector>

#include "vb/core/ids.hpp"
#include "vb/core/math.hpp"
#include "vb/worldgen/biome_selector.hpp"

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

// Schematic-only decoration (Phase 6.14 scope note: procedural/callback-based
// decoration is explicitly deferred -- see REMAINING_TASKS.md's Deferred
// section -- since a per-site Lua callback can't run on a worldgen worker
// thread, same reasoning as set_pipeline itself taking data, not a
// function).
struct DecorationEntry {
	struct BlockOffset {
		core::IVec3 offset;
		core::BlockId block;
	};
	std::vector<BlockOffset> blocks; // relative to the scatter anchor
	double spawn_rate = 0.0; // expected placements per chunk column
};

struct PackWorldGenPipeline {
	std::function<double(double world_x, double world_z)> height_field;
	int sea_level = 62;
	int soil_depth = 4;

	BiomeSelector biomes;
	std::vector<CarverDef> carvers;
	std::vector<VeinDef> veins;
	// Indexed by biome index (matches `biomes`'s own order); may be shorter
	// than biome_count() if trailing biomes have no decoration entries.
	std::vector<std::vector<DecorationEntry>> decoration;

	const std::vector<DecorationEntry> &decoration_for(std::size_t biome_index) const {
		static const std::vector<DecorationEntry> kEmpty;
		return biome_index < decoration.size() ? decoration[biome_index] : kEmpty;
	}
};

} // namespace vb::worldgen
