#pragma once

#include <cstdint>

#include "vb/core/noise.hpp"

namespace vb::worldgen {

// Small deterministic counter-based PRNG for vein/decoration scatter and the
// structure editor's generators -- built on the same dependency-free integer
// hashing as vb/core/noise.hpp (no <random>, so results stay bit-identical
// across platforms/compilers, same determinism requirement that file
// documents).
struct DetRng {
	std::uint64_t state;

	double next01() {
		state = core::noise::mix64(state);
		return core::noise::to_unit(state);
	}

	// Uniform in [0, n). `n` must be > 0.
	std::int64_t next_index(std::int64_t n) {
		state = core::noise::mix64(state);
		return static_cast<std::int64_t>(state % static_cast<std::uint64_t>(n));
	}
};

} // namespace vb::worldgen
