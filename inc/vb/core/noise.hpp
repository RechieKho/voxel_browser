#pragma once

#include <cmath>
#include <cstdint>

// Deterministic, dependency-free coherent noise. Uses only integer hashing and
// polynomial interpolation (no sin/cos), so results are bit-identical across
// platforms and compilers with default (IEEE-754, non-fast-math) settings —
// required for the worldgen determinism CI gate (spec §18).
//
// FastNoise2 becomes an optional backend for the Lua-driven pipeline in Phase 4
// (VB_WITH_WORLDGEN); this is the fixed base pipeline's generator.

namespace vb::core::noise {

constexpr std::uint64_t mix64(std::uint64_t x) {
	x ^= x >> 33;
	x *= 0xff51afd7ed558ccdULL;
	x ^= x >> 33;
	x *= 0xc4ceb9fe1a85ec53ULL;
	x ^= x >> 33;
	return x;
}

inline std::uint64_t hash2(std::uint64_t seed, std::int64_t x, std::int64_t y) {
	const std::uint64_t ux = static_cast<std::uint64_t>(x);
	const std::uint64_t uy = static_cast<std::uint64_t>(y);
	return mix64(seed ^ mix64(ux * 0x9E3779B97F4A7C15ULL) ^
			mix64(uy * 0xC2B2AE3D27D4EB4FULL));
}

// Hash -> double in [0, 1).
inline double to_unit(std::uint64_t h) {
	return static_cast<double>(h >> 11) * (1.0 / 9007199254740992.0);
}

constexpr double smoothstep5(double t) {
	return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
}

constexpr double lerp(double a, double b, double t) { return a + (b - a) * t; }

// Value noise in [0, 1).
inline double value2(std::uint64_t seed, double x, double y) {
	const double fx = std::floor(x);
	const double fy = std::floor(y);
	const auto ix = static_cast<std::int64_t>(fx);
	const auto iy = static_cast<std::int64_t>(fy);
	const double tx = smoothstep5(x - fx);
	const double ty = smoothstep5(y - fy);

	const double v00 = to_unit(hash2(seed, ix, iy));
	const double v10 = to_unit(hash2(seed, ix + 1, iy));
	const double v01 = to_unit(hash2(seed, ix, iy + 1));
	const double v11 = to_unit(hash2(seed, ix + 1, iy + 1));
	return lerp(lerp(v00, v10, tx), lerp(v01, v11, tx), ty);
}

struct FbmParams {
	int octaves = 4;
	double frequency = 1.0;
	double lacunarity = 2.0;
	double gain = 0.5;
};

// Fractal Brownian motion, normalized to [0, 1).
inline double fbm2(std::uint64_t seed, double x, double y, const FbmParams &p) {
	double amp = 1.0;
	double freq = p.frequency;
	double sum = 0.0;
	double norm = 0.0;
	for (int o = 0; o < p.octaves; ++o) {
		sum += amp *
				value2(seed + static_cast<std::uint64_t>(o) * 0x9E3779B9ULL,
						x * freq, y * freq);
		norm += amp;
		amp *= p.gain;
		freq *= p.lacunarity;
	}
	return norm > 0.0 ? sum / norm : 0.0;
}

} // namespace vb::core::noise
