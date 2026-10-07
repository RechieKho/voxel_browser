#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "vb/core/ids.hpp"
#include "vb/core/math.hpp"
#include "vb/core/noise.hpp"
#include "vb/world/block.hpp"
#include "vb/world/chunk.hpp"
#include "vb/worldgen/pipeline.hpp"

// The fixed base worldgen pipeline (spec §6, steps 1-3 + basic lighting hook)
// PLUS, as of Phase 6.14, an optional pack-driven pipeline on top of it.
// Deterministic from (seed, chunk coord). When `WorldGenerator` is
// constructed without a `PackWorldGenPipeline` (the common case -- no pack
// ever calls `vb.worldgen.set_pipeline`), `generate()` runs the exact same
// hardcoded fBm-heightmap body it always has (byte-identical output,
// `tests/unit/worldgen_test.cpp`'s golden-hash gate is unaffected). With one
// attached, `generate()` instead samples the pipeline's height field, resolves
// a biome per column (Voronoi/adjacency-weighted, see
// vb/worldgen/biome_selector.hpp), then runs carver/vein/decoration passes.

namespace vb::worldgen {

// One structure placement chosen by the pull-stamping pass
// (docs/structure-editor.md §E). Pure function of (seed, pipeline, anchor
// column), so every chunk that overlaps it reaches the same answer.
struct StructurePlacement {
	int x = 0; // anchor world column
	int z = 0;
	int ground_y = 0; // surface height; the anchor cell sits at ground_y + 1
	std::uint32_t biome = 0;
	std::uint32_t rule = 0; // PlacementRule index within the biome's list
	std::uint32_t variant = 0;
	int rotation = 0; // quarter turns about +y
	bool mirror = false;

	bool operator==(const StructurePlacement &) const = default;
};

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
	// `pipeline` is nullptr for the fixed default path (the overwhelming
	// common case). Non-null when a pack called `vb.worldgen.set_pipeline`
	// -- see PackRuntime::build_worldgen_pipeline
	// (src/script/pack_runtime.cpp), which is the only place that builds
	// one. Shared (not owned uniquely) because both the server's
	// WorldGenWorkerPool and its own construction path may want the same
	// immutable pipeline.
	WorldGenerator(WorldGenParams params, const world::BlockRegistry &registry,
			std::shared_ptr<const PackWorldGenPipeline> pipeline = nullptr);

	// Fills `chunk` with terrain and marks it Generated. `chunk` must already
	// carry its ChunkCoord.
	void generate(world::Chunk &chunk) const;

	// Surface height (integer world Y of the topmost solid voxel) at a world
	// column. Exposed for tests and for decoration placement later.
	int surface_height(int world_x, int world_z) const;

	// Sea level in effect for this generator -- the pipeline's own value when
	// a PackWorldGenPipeline is attached (it can differ from `params_`, which
	// only governs the fixed default path), otherwise `params_.sea_level`.
	int sea_level() const;

	// The block this generator would put at a world voxel *before* the
	// vein and structure passes run: terrain, carvers and water only. A pure
	// function of (seed, pipeline, position); structure anchors are validated
	// against it so they never depend on a neighboring chunk's voxels.
	core::BlockId block_at_pregen(int world_x, int world_y, int world_z) const;

	// Every structure placement whose anchor column lies in the inclusive
	// range [x0, x1] x [z0, z1], in the canonical stamp order (anchor x, then
	// z, then rule). Empty without a pack pipeline. Used by generate() and by
	// the structure editor's preview counters.
	std::vector<StructurePlacement> structure_placements(
			int x0, int z0, int x1, int z1) const;

	const WorldGenParams &params() const { return params_; }
	const PackWorldGenPipeline *pipeline() const { return pipeline_.get(); }

private:
	// One world column's terrain recipe (pipeline path).
	struct Column {
		int height = 0;
		std::size_t biome = 0; // index into the pipeline's BiomeSelector
		core::BlockId surface = core::BlockId::kAir;
		core::BlockId filler = core::BlockId::kAir;
		core::BlockId stone = core::BlockId::kAir;
	};
	Column column_at(int world_x, int world_z) const;
	core::BlockId pregen_block(const Column &col, int world_x, int world_y, int world_z) const;
	// Stamps every placement overlapping `chunk` (pipeline path only).
	void stamp_structures(world::Chunk &chunk) const;

	WorldGenParams params_;
	core::BlockId air_;
	core::BlockId stone_;
	core::BlockId dirt_;
	core::BlockId grass_;
	core::BlockId sand_;
	core::BlockId water_;
	std::shared_ptr<const PackWorldGenPipeline> pipeline_;
};

// A safe default spawn point for this generator: standing on the surface at
// world column (spawn_x, spawn_z), or the nearest column that is dry land if
// that one is underwater. `JoinGrant::spawn_pos` used to default to a fixed
// {0, 64, 0} regardless of seed -- with base_height=64 and amplitude=28, the
// real surface height ranges roughly [36, 92], so a fixed Y had a real
// chance of landing at or below it, spawning the player embedded in solid
// terrain with no fall involved (see STATE.md). It later stood on the exact
// (spawn_x, spawn_z) surface, but that column can itself be ocean -- surface
// height there sits at or below sea level -- which spawned the player
// floating in open water instead of on land. This now spirals outward from
// (spawn_x, spawn_z) in a growing square ring, one column at a time, until
// it finds one whose surface sits above sea level, and spawns there instead.
// A dry column also has to be standable in the *generated* world (the
// heightmap alone ignores structures and carvers): its surface voxel still
// solid and the two voxels above it air -- otherwise a tree trunk, boulder or
// cave mouth on that exact column spawned the player inside it. Pass the
// same generator (pipeline included) the world itself uses, or the heights
// won't match the terrain the player actually lands in.
// Callers (client `--singleplayer` and the dedicated server) feed this into
// a HandshakeServerHost::on_ready so JoinGrant::spawn_pos is seed-correct
// instead of a guess.
core::Vec3d default_spawn_position(
		const WorldGenerator &gen, int spawn_x = 0, int spawn_z = 0);

} // namespace vb::worldgen
