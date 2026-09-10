#pragma once

#include <cstdint>

#include "vb/world/block.hpp"
#include "vb/world/chunk.hpp"

// Voxel lighting (spec §5.4). Flood-fill sky + block light. Phase 2 computes it
// per chunk assuming open sky directly above the chunk; cross-chunk sky
// occlusion and incremental relight-on-edit are Phase 3/5 refinements that reuse
// the same propagation core (propagate_block_light / propagate_sky_light).

namespace vb::world {

inline constexpr std::uint8_t kMaxLight = 15;

class LightEngine {
public:
	explicit LightEngine(const BlockRegistry &registry) : registry_(registry) {}

	// Recompute both light channels for a whole chunk from scratch. Clears the
	// light dirty flag.
	void relight_chunk(Chunk &chunk) const;

private:
	// How much light a block passes through (kMaxLight for air/transparent,
	// reduced for liquids, 0 for opaque).
	std::uint8_t transmittance(core::BlockId block) const;

	const BlockRegistry &registry_;
};

} // namespace vb::world
