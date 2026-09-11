#pragma once

#include <cstdint>

#include "vb/core/ids.hpp"
#include "vb/core/math.hpp"
#include "vb/core/noise.hpp"
#include "vb/world/block.hpp"
#include "vb/world/chunk.hpp"

// The fixed base worldgen pipeline (spec §6, steps 1–3 + basic lighting hook).
// Deterministic from (seed, chunk coord). Phase 4 replaces this with a
// Lua-configured FastNoise2 pipeline; the interface (generate one chunk) stays.

namespace vb::worldgen {

struct WorldGenParams {
	std::uint64_t seed = 0;
	int sea_level = 62;
	double base_height = 64.0; // mean terrain height
	double amplitude = 28.0; // peak-to-mean height variation
	core::noise::FbmParams height_noise{ /*octaves*/ 5, /*freq*/ 1.0 / 96.0,
		/*lac*/ 2.0, /*gain*/ 0.5 };
	int soil_depth = 4; // dirt layers under the surface
};

class WorldGenerator {
public:
	WorldGenerator(WorldGenParams params, const world::BlockRegistry &registry);

	// Fills `chunk` with terrain and marks it Generated. `chunk` must already
	// carry its ChunkCoord.
	void generate(world::Chunk &chunk) const;

	// Surface height (integer world Y of the topmost solid voxel) at a world
	// column. Exposed for tests and for decoration placement later.
	int surface_height(int world_x, int world_z) const;

	const WorldGenParams &params() const { return params_; }

private:
	WorldGenParams params_;
	core::BlockId air_;
	core::BlockId stone_;
	core::BlockId dirt_;
	core::BlockId grass_;
	core::BlockId sand_;
	core::BlockId water_;
};

// A safe default spawn point for this generator: standing on the surface at
// world column (spawn_x, spawn_z). `JoinGrant::spawn_pos` used to default to
// a fixed {0, 64, 0} regardless of seed -- with base_height=64 and
// amplitude=28, the real surface height ranges roughly [36, 92], so a fixed
// Y had a real chance of landing at or below it, spawning the player
// embedded in solid terrain with no fall involved (see STATE.md). Callers
// (client `--singleplayer` and the dedicated server) feed this into a
// HandshakeServerHost::on_ready so JoinGrant::spawn_pos is seed-correct
// instead of a guess.
core::Vec3d default_spawn_position(
		const WorldGenerator &gen, int spawn_x = 0, int spawn_z = 0);

} // namespace vb::worldgen
