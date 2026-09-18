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
inline constexpr std::uint64_t kHashZ = 0x165667B19E3779F9ULL;
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

inline std::uint64_t hash3(std::uint64_t seed, std::int64_t x, std::int64_t y, std::int64_t z) {
	const std::uint64_t ux = static_cast<std::uint64_t>(x);
	const std::uint64_t uy = static_cast<std::uint64_t>(y);
	const std::uint64_t uz = static_cast<std::uint64_t>(z);
	return mix64(seed ^ mix64(ux * kHashX) ^ mix64(uy * kHashY) ^ mix64(uz * kHashZ));
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

// Value noise in [0, 1), 3D (trilinear).
inline double value3(std::uint64_t seed, double x, double y, double z) {
	const double fx = std::floor(x);
	const double fy = std::floor(y);
	const double fz = std::floor(z);
	const auto ix = static_cast<std::int64_t>(fx);
	const auto iy = static_cast<std::int64_t>(fy);
	const auto iz = static_cast<std::int64_t>(fz);
	const double tx = smoothstep5(x - fx);
	const double ty = smoothstep5(y - fy);
	const double tz = smoothstep5(z - fz);

	const double v000 = to_unit(hash3(seed, ix, iy, iz));
	const double v100 = to_unit(hash3(seed, ix + 1, iy, iz));
	const double v010 = to_unit(hash3(seed, ix, iy + 1, iz));
	const double v110 = to_unit(hash3(seed, ix + 1, iy + 1, iz));
	const double v001 = to_unit(hash3(seed, ix, iy, iz + 1));
	const double v101 = to_unit(hash3(seed, ix + 1, iy, iz + 1));
	const double v011 = to_unit(hash3(seed, ix, iy + 1, iz + 1));
	const double v111 = to_unit(hash3(seed, ix + 1, iy + 1, iz + 1));

	const double x00 = lerp(v000, v100, tx);
	const double x10 = lerp(v010, v110, tx);
	const double x01 = lerp(v001, v101, tx);
	const double x11 = lerp(v011, v111, tx);
	return lerp(lerp(x00, x10, ty), lerp(x01, x11, ty), tz);
}

// F1 cellular (Voronoi) noise in 2D: jitters one site per unit cell (jitter in
// [0,1) of the cell's own width, deterministic from (seed, cell)) and returns
// the normalized distance to the nearest site over the 3x3 neighborhood, plus
// a hash identifying which cell owns it -- the latter is what a Voronoi-cell
// partition (e.g. biome selection) keys off, the former is what a "cellular"
// noise node samples as a scalar field.
struct Cellular2Result {
	double distance; // to nearest site, roughly [0, ~1.5]
	std::int64_t cell_x;
	std::int64_t cell_y;
};

inline Cellular2Result cellular2(std::uint64_t seed, double x, double y) {
	const auto cx = static_cast<std::int64_t>(std::floor(x));
	const auto cy = static_cast<std::int64_t>(std::floor(y));
	double best_dist = 1e18;
	std::int64_t best_x = cx;
	std::int64_t best_y = cy;
	for (std::int64_t oy = -1; oy <= 1; ++oy) {
		for (std::int64_t ox = -1; ox <= 1; ++ox) {
			const std::int64_t nx = cx + ox;
			const std::int64_t ny = cy + oy;
			const std::uint64_t h = hash2(seed, nx, ny);
			const double jx = to_unit(h);
			const double jy = to_unit(mix64(h));
			const double sx = static_cast<double>(nx) + jx;
			const double sy = static_cast<double>(ny) + jy;
			const double dx = sx - x;
			const double dy = sy - y;
			const double d = std::sqrt(dx * dx + dy * dy);
			if (d < best_dist) {
				best_dist = d;
				best_x = nx;
				best_y = ny;
			}
		}
	}
	return { best_dist, best_x, best_y };
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
