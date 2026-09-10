#pragma once

#include <unordered_set>
#include <vector>

#include "vb/core/ids.hpp"
#include "vb/world/lighting.hpp"
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

private:
	World &world_;
	worldgen::WorldGenWorkerPool &pool_;
	LightEngine light_;
	std::unordered_set<core::ChunkCoord> requested_;
	std::vector<core::ChunkCoord> newly_ready_;
	std::vector<core::ChunkCoord> unloaded_;
};

} // namespace vb::world
