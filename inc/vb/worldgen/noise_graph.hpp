#pragma once

#include <cstdint>
#include <memory>

// Small composable noise-graph IR (Phase 6.14, spec §6 stage 1). Built once
// (parsed from a pack's `vb.noise.*`-built Lua table, see
// PackRuntime::build_worldgen_pipeline in src/script/pack_runtime.cpp) into
// an immutable tree of these nodes -- no Lua references retained, so it's
// safe to evaluate from WorldGenWorkerPool's worker threads with no locking,
// same as WorldGenerator's own const-only threading posture.
//
// Two evaluators exist for this same IR: the always-available one in
// src/worldgen/noise_graph.cpp built on vb/core/noise.hpp's dependency-free
// hash noise (used whenever VB_WITH_WORLDGEN is off, i.e. every default
// build), and -- when VB_WITH_WORLDGEN is on -- a FastNoise2 SmartNode
// compiler (same file, #if-guarded) that translates this tree into a real
// FastNoise2 node graph once at pipeline-build time.

namespace vb::worldgen {

enum class NoiseNodeType : std::uint8_t {
	kConstant,
	kValue,
	kCellular,
	kFbm,
	kRemap,
	kCombine,
};

enum class NoiseCombineOp : std::uint8_t { kAdd, kMultiply, kMin, kMax };

struct NoiseNode;
using NoiseNodePtr = std::shared_ptr<const NoiseNode>;

// Every field not relevant to `type` is simply unused -- a flat struct is far
// simpler than a variant for a tree this small and short-lived (built once
// per pipeline, never mutated).
struct NoiseNode {
	NoiseNodeType type = NoiseNodeType::kValue;

	// Every node index gets its own salt (assigned in parse order) so two
	// structurally-identical nodes in the same graph (e.g. `combine(value,
	// value)`) don't sample the exact same field -- same reasoning as
	// fbm2()'s per-octave salt in vb/core/noise.hpp.
	std::uint64_t salt = 0;

	// kConstant
	double constant_value = 0.0;

	// kValue / kCellular / kFbm's own base sampling frequency.
	double frequency = 1.0;

	// kFbm (wraps `source`) / kRemap (transforms `source`).
	NoiseNodePtr source;
	int octaves = 4;
	double lacunarity = 2.0;
	double gain = 0.5;

	// kRemap
	double in_min = 0.0;
	double in_max = 1.0;
	double out_min = 0.0;
	double out_max = 1.0;

	// kCombine
	NoiseNodePtr a;
	NoiseNodePtr b;
	NoiseCombineOp op = NoiseCombineOp::kAdd;

	// Evaluates in roughly [0, 1) for kValue/kCellular/kFbm leaves; kRemap/
	// kCombine/kConstant can land outside that range on purpose (that's the
	// point of remap/combine). `seed` is the pipeline/world seed; `salt`
	// above disambiguates nodes, not chunks/columns.
	double eval2(std::uint64_t seed, double x, double y) const;
	double eval3(std::uint64_t seed, double x, double y, double z) const;
};

} // namespace vb::worldgen
