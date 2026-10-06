#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "vb/core/math.hpp"
#include "vb/editor/block_catalog.hpp"
#include "vb/world/client_chunk_store.hpp"
#include "vb/worldgen/generator.hpp"
#include "vb/worldgen/structure.hpp"
#include "vb/worldgen/worker_pool.hpp"

// Live terrain preview (docs/structure-editor.md §I, S6): the structure being
// edited -- in memory, not yet saved -- placed by its own placement rule with
// the engine's real placement code on a patch of generated test terrain. The
// test terrain is a one-biome PackWorldGenPipeline built in C++, so no pack
// worldgen code runs and no server is involved.

namespace vb::editor {

struct TerrainConfig {
	int base_height = 64; // mean surface height
	double amplitude = 4.0; // 0 = flat, larger = hillier
	double scale = 48.0; // feature size in blocks
	int sea_level = 58;
	int soil_depth = 4;
	std::string surface = "base:grass";
	std::string filler = "base:dirt";
	std::string stone = "base:stone";
	std::uint64_t seed = 1;
	// The patch is `patch` x `patch` columns of 32x32 blocks.
	int patch = 8;
};

struct PreviewInput {
	TerrainConfig terrain;
	// Expected placements per 32x32 column for every structure. Preview only:
	// real density is set by the biome entry that uses the structure.
	double spawn_rate = 1.0;
	// structures[0] is the one being edited; any others are the folder's other
	// structures, placed too when `include_others` is set.
	std::vector<worldgen::StructureSpec> structures;
	bool include_others = false;
};

struct PreviewStats {
	// Why the preview couldn't be built (an unknown block name, an invalid
	// structure); empty when it was.
	std::string error;
	std::vector<std::string> warnings;
	std::size_t edited_placements = 0;
	std::size_t other_placements = 0;
	int patch = 0;
	int surface_min = 0;
	int surface_max = 0;
};

class TerrainPreview {
public:
	explicit TerrainPreview(const BlockCatalog &catalog);
	~TerrainPreview();

	TerrainPreview(const TerrainPreview &) = delete;
	TerrainPreview &operator=(const TerrainPreview &) = delete;

	// Drops whatever was generated, cancels pending chunks, and starts again.
	// The pipeline, placement counts and warnings are ready when this
	// returns; chunks arrive over the next polls.
	void start(const PreviewInput &input);

	// Moves finished chunks into the store (full-bright). True when any
	// arrived.
	bool poll();
	// True while chunks are still being generated.
	bool busy() const { return expected_ > received_; }
	bool started() const { return started_; }

	const world::ClientChunkStore &store() const { return store_; }
	const PreviewStats &stats() const { return stats_; }
	// The generator behind the current preview (null before start() or on an
	// error), for tests and picking.
	const worldgen::WorldGenerator *generator() const { return generator_.get(); }

	// A fly-camera starting position and look direction that frames the patch.
	core::Vec3d spawn_point() const { return spawn_; }

private:
	const BlockCatalog &catalog_;
	world::ClientChunkStore store_;
	std::unique_ptr<worldgen::WorldGenerator> generator_;
	std::unique_ptr<worldgen::WorldGenWorkerPool> pool_;
	PreviewStats stats_;
	core::Vec3d spawn_{ 0.0, 0.0, 0.0 };
	std::size_t expected_ = 0;
	std::size_t received_ = 0;
	std::uint64_t epoch_ = 0;
	bool started_ = false;
};

// Builds the test-terrain pipeline for `input` (exposed for tests): the
// terrain, the resolved structures, and one placement rule per structure.
// Returns null and sets `error` on an unknown block name or an invalid
// structure.
std::shared_ptr<const worldgen::PackWorldGenPipeline> build_preview_pipeline(
		const BlockCatalog &catalog, const PreviewInput &input, std::string &error);

} // namespace vb::editor
