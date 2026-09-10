#include "vb/world/chunk_lifecycle.hpp"

#include <algorithm>
#include <utility>

namespace vb::world {

ChunkLifecycleSystem::ChunkLifecycleSystem(World &world,
		worldgen::WorldGenWorkerPool &pool, const BlockRegistry &registry) : world_(world),
																			 pool_(pool),
																			 light_(registry) {}

void ChunkLifecycleSystem::update(const std::vector<core::ChunkCoord> &desired) {
	newly_ready_.clear();
	unloaded_.clear();

	const std::unordered_set<core::ChunkCoord> wanted(desired.begin(),
			desired.end());

	auto ingest = [&] {
		for (auto &chunk : pool_.poll_completed()) {
			const core::ChunkCoord coord = chunk->coord();
			requested_.erase(coord);
			if (wanted.count(coord) == 0) {
				continue;
			}
			light_.relight_chunk(*chunk);
			chunk->set_gen_state(GenState::kGenerated);
			world_.insert_chunk(std::move(chunk));
			newly_ready_.push_back(coord);
		}
	};

	// 1. Ingest chunks finished since last update.
	ingest();

	// 2. Request wanted chunks that are neither loaded nor in flight.
	for (core::ChunkCoord coord : desired) {
		if (world_.has_chunk(coord) || requested_.count(coord) != 0) {
			continue;
		}
		if (pool_.submit(coord)) {
			requested_.insert(coord);
		}
	}

	// 2b. A synchronous pool finished those immediately — ingest again so the
	//     integrated server streams a chunk the same tick it's requested.
	ingest();

	// 3. Unload loaded chunks nobody wants.
	for (core::ChunkCoord coord : world_.loaded_coords()) {
		if (wanted.count(coord) == 0) {
			world_.unload_chunk(coord);
			unloaded_.push_back(coord);
		}
	}
	// Also forget in-flight requests nobody wants any more (they'll arrive and
	// be dropped on the next ingest since they won't be re-inserted... actually
	// they still get inserted; drop them then).
	for (auto it = requested_.begin(); it != requested_.end();) {
		if (wanted.count(*it) == 0) {
			it = requested_.erase(it);
		} else {
			++it;
		}
	}
}

} // namespace vb::world
