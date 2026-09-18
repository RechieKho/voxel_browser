#pragma once

// Only declared (and only ever compiled) when VB_WITH_WORLDGEN links real
// FastNoise2 (cmake/Dependencies.cmake, src/core/CMakeLists.txt). Compiles
// the always-available worldgen::NoiseNode IR (vb/worldgen/noise_graph.hpp)
// into a real FastNoise2 SmartNode graph, once, at pipeline-build time
// (PackRuntime::build_worldgen_pipeline, src/script/pack_runtime.cpp) --
// mirrors what NoiseNode::eval2/eval3 do by hand against vb/core/noise.hpp
// when this optional backend isn't linked. The two backends are NOT expected
// to produce bit-identical output (different underlying noise algorithms
// entirely) -- both just need to be deterministic and reasonably shaped for
// the same graph description. See REMAINING_TASKS.md 6.14 / STATE.md for the
// full writeup of this trade.

#if VB_WITH_WORLDGEN

#include <cstdint>
#include <functional>

#include "vb/worldgen/noise_graph.hpp"

namespace vb::worldgen {

// Builds a FastNoise2-backed evaluator for `node` (nullptr treated as an
// always-0 constant). The returned closure owns the compiled SmartNode graph
// by value (FastNoise2's own reference counting is atomic/mutex-protected --
// confirmed safe to copy/evaluate concurrently from WorldGenWorkerPool's
// worker threads, same lock-free-after-construction posture as the
// hand-rolled NoiseNode path).
std::function<double(double x, double y)> compile_fastnoise2_2d(
		const NoiseNodePtr &node, std::uint64_t seed);
std::function<double(double x, double y, double z)> compile_fastnoise2_3d(
		const NoiseNodePtr &node, std::uint64_t seed);

} // namespace vb::worldgen

#endif // VB_WITH_WORLDGEN
