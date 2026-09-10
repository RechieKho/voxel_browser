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

// All constants are std::uint64_t so the arithmetic never mixes it with
// `unsigned long long` (a distinct type on LP64), which trips GCC -Wsign-conversion.
inline constexpr std::uint64_t kMixA = 0xff51afd7ed558ccdULL;
inline constexpr std::uint64_t kMixB = 0xc4ceb9fe1a85ec53ULL;
inline constexpr std::uint64_t kHashX = 0x9E3779B97F4A7C15ULL;
inline constexpr std::uint64_t kHashY = 0xC2B2AE3D27D4EB4FULL;
inline constexpr std::uint64_t kOctaveSalt = 0x9E3779B97F4A7C15ULL;

constexpr std::uint64_t mix64(std::uint64_t x) {
	x ^= x >> 33;
	x *= kMixA;
	x ^= x >> 33;
	x *= kMixB;
	x ^= x >> 33;
	return x;
}

inline std::uint64_t hash2(std::uint64_t seed, std::int64_t x, std::int64_t y) {
	const std::uint64_t ux = static_cast<std::uint64_t>(x);
	const std::uint64_t uy = static_cast<std::uint64_t>(y);
	return mix64(seed ^ mix64(ux * kHashX) ^ mix64(uy * kHashY));
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
		const std::uint64_t octave_seed =
				seed + static_cast<std::uint64_t>(o) * kOctaveSalt;
		sum += amp * value2(octave_seed, x * freq, y * freq);
		norm += amp;
		amp *= p.gain;
		freq *= p.lacunarity;
	}
	return norm > 0.0 ? sum / norm : 0.0;
}

} // namespace vb::core::noise
