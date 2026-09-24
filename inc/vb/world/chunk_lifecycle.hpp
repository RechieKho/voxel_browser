#pragma once

#include <deque>
#include <memory>
#include <unordered_set>
#include <vector>

#include "vb/core/ids.hpp"
#include "vb/world/lighting.hpp"
#include "vb/world/region_store.hpp"
#include "vb/world/world.hpp"
#include "vb/worldgen/worker_pool.hpp"

// ChunkLifecycleSystem (spec §7.2 step 9 / §8.5): keeps the World's loaded set
// aligned with what players are interested in. Generation runs on the worldgen
// pool; finished chunks are lit and inserted here on the tick thread. Chunks no
// player wants are unloaded.

namespace vb::world {

class ChunkLifecycleSystem {
public:
	ChunkLifecycleSystem(World &world, worldgen::WorldGenWorkerPool &pool,
			const BlockRegistry &registry);

	// `desired` = union of every player's view set (sorted or not). Submits
	// missing chunks, ingests finished ones, unloads unwanted ones.
	void update(const std::vector<core::ChunkCoord> &desired);

	// Chunks that became loaded+lit during the last update() (for replication).
	const std::vector<core::ChunkCoord> &newly_ready() const {
		return newly_ready_;
	}
	// Chunks unloaded during the last update().
	const std::vector<core::ChunkCoord> &unloaded() const { return unloaded_; }

	std::size_t requested_count() const { return requested_.size(); }

	// World persistence (opt-in, nullptr = disabled -- same posture as every
	// other optional engine seam in this codebase): when set, update() loads a
	// requested chunk from disk instead of regenerating it if one was saved
	// there, and saves an edited chunk's current state before evicting it on
	// unload. Does not itself autosave a chunk that stays loaded indefinitely
	// -- that's the caller's periodic sweep (see RegionStore::save_if_dirty).
	void set_region_store(RegionStore *store) { region_store_ = store; }

	// Overrides the default 32-chunks-per-update() ingest budget (see
	// chunk_lifecycle.cpp's kDefaultIngestBudgetPerTick comment for why it
	// exists at all). Callers that invoke update() more than once per real
	// frame -- see Singleplayer::tick()'s fixed-step catch-up loop in
	// src/client/main.cpp -- should shrink this proportionally so the *frame*
	// pays the intended budget instead of update()'s caller count silently
	// multiplying it; a caller that ticks once per frame never needs this.
	void set_ingest_budget(std::size_t chunks_per_update) {
		ingest_budget_ = chunks_per_update;
	}

private:
	World &world_;
	worldgen::WorldGenWorkerPool &pool_;
	LightEngine light_;
	RegionStore *region_store_ = nullptr;
	std::size_t ingest_budget_ = 32; // see set_ingest_budget() / chunk_lifecycle.cpp
	std::unordered_set<core::ChunkCoord> requested_;
	std::vector<core::ChunkCoord> newly_ready_;
	std::vector<core::ChunkCoord> unloaded_;
	// Chunks the worldgen pool has already finished but update() hasn't yet
	// inserted+relit (paced by kIngestBudgetPerTick in chunk_lifecycle.cpp).
	std::deque<std::unique_ptr<Chunk>> backlog_;
};

} // namespace vb::world
