#include "vb/worldgen/noise_graph.hpp"

#include <algorithm>

#include "vb/core/noise.hpp"

namespace vb::worldgen {

namespace {

double clamp01(double v) { return std::clamp(v, 0.0, 1.0); }

double remap(double v, double in_min, double in_max, double out_min, double out_max) {
	const double span = in_max - in_min;
	const double t = span != 0.0 ? (v - in_min) / span : 0.0;
	return out_min + t * (out_max - out_min);
}

double combine(NoiseCombineOp op, double a, double b) {
	switch (op) {
		case NoiseCombineOp::kAdd:
			return a + b;
		case NoiseCombineOp::kMultiply:
			return a * b;
		case NoiseCombineOp::kMin:
			return std::min(a, b);
		case NoiseCombineOp::kMax:
			return std::max(a, b);
	}
	return a;
}

} // namespace

double NoiseNode::eval2(std::uint64_t seed, double x, double y) const {
	const std::uint64_t node_seed = seed + salt * core::noise::kOctaveSalt;
	switch (type) {
		case NoiseNodeType::kConstant:
			return constant_value;
		case NoiseNodeType::kValue:
			return core::noise::value2(node_seed, x * frequency, y * frequency);
		case NoiseNodeType::kCellular: {
			const auto r = core::noise::cellular2(node_seed, x * frequency, y * frequency);
			return clamp01(r.distance / 1.5);
		}
		case NoiseNodeType::kFbm: {
			if (!source) {
				return 0.0;
			}
			double amp = 1.0;
			double freq = frequency;
			double sum = 0.0;
			double norm = 0.0;
			for (int o = 0; o < octaves; ++o) {
				const std::uint64_t octave_seed =
						node_seed + static_cast<std::uint64_t>(o) * core::noise::kOctaveSalt;
				sum += amp * source->eval2(octave_seed, x * freq, y * freq);
				norm += amp;
				amp *= gain;
				freq *= lacunarity;
			}
			return norm > 0.0 ? sum / norm : 0.0;
		}
		case NoiseNodeType::kRemap: {
			if (!source) {
				return 0.0;
			}
			return remap(source->eval2(seed, x, y), in_min, in_max, out_min, out_max);
		}
		case NoiseNodeType::kCombine: {
			const double av = a ? a->eval2(seed, x, y) : 0.0;
			const double bv = b ? b->eval2(seed, x, y) : 0.0;
			return combine(op, av, bv);
		}
	}
	return 0.0;
}

double NoiseNode::eval3(std::uint64_t seed, double x, double y, double z) const {
	const std::uint64_t node_seed = seed + salt * core::noise::kOctaveSalt;
	switch (type) {
		case NoiseNodeType::kConstant:
			return constant_value;
		case NoiseNodeType::kValue:
			return core::noise::value3(
					node_seed, x * frequency, y * frequency, z * frequency);
		case NoiseNodeType::kCellular: {
			// No 3D cellular primitive -- approximate by sampling the 2D
			// cellular field on (x, z) only. Cellular's primary intended use
			// is the 2D biome-cell partition; carvers/veins should prefer
			// kValue/kFbm for genuine 3D density fields.
			const auto r = core::noise::cellular2(node_seed, x * frequency, z * frequency);
			return clamp01(r.distance / 1.5);
		}
		case NoiseNodeType::kFbm: {
			if (!source) {
				return 0.0;
			}
			double amp = 1.0;
			double freq = frequency;
			double sum = 0.0;
			double norm = 0.0;
			for (int o = 0; o < octaves; ++o) {
				const std::uint64_t octave_seed =
						node_seed + static_cast<std::uint64_t>(o) * core::noise::kOctaveSalt;
				sum += amp * source->eval3(octave_seed, x * freq, y * freq, z * freq);
				norm += amp;
				amp *= gain;
				freq *= lacunarity;
			}
			return norm > 0.0 ? sum / norm : 0.0;
		}
		case NoiseNodeType::kRemap: {
			if (!source) {
				return 0.0;
			}
			return remap(source->eval3(seed, x, y, z), in_min, in_max, out_min, out_max);
		}
		case NoiseNodeType::kCombine: {
			const double av = a ? a->eval3(seed, x, y, z) : 0.0;
			const double bv = b ? b->eval3(seed, x, y, z) : 0.0;
			return combine(op, av, bv);
		}
	}
	return 0.0;
}

} // namespace vb::worldgen
