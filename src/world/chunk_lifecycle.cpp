#include "vb/world/chunk_lifecycle.hpp"

#include <algorithm>
#include <utility>

#include "vb/world/lighting.hpp"

namespace vb::world {

// Default (see ChunkLifecycleSystem::ingest_budget_ / set_ingest_budget()):
// caps how many just-finished chunks get inserted+relit per update() call.
// relight_column() cascades down through a whole loaded column per chunk
// (up to ~2*vertical_view+1 relight_chunk() calls each), so ingesting an
// unbounded batch is `O(chunks_finished_since_last_tick)` per call -- fine
// normally, but a large initial view distance (e.g. render_distance=8's
// ~2000-chunk box) submits its entire box to the worldgen pool in one go,
// and background threads can finish faster than the main thread can insert
// +relight them. Without a cap, a slower-than-usual tick lets even more
// finish before the next one, so each call's batch -- and its cost -- keeps
// growing: an unbounded feedback loop that stalled the main thread for
// upwards of a minute in testing (see world_replication_test.cpp's "DIAG"
// case during Phase 7.1's loading-screen investigation) instead of the
// expected few milliseconds. Anything the pool finished beyond this budget
// just waits in `backlog_` for the next update() call(s) -- polling it off
// the pool is still unbounded (cheap: just moves pointers), only the
// insert+relight work is paced.
//
// This constant assumes update() runs once per real tick interval, which is
// true for a dedicated server (src/server/main.cpp sleeps between ticks) but
// NOT for --singleplayer's in-process server: Singleplayer::tick()
// (src/client/main.cpp) catches up on several fixed 1/20s steps inside one
// rendered frame after any stall, calling this budget's full amount on each
// step -- up to kMaxStepsPerFrame=5x in a single frame, which is itself
// enough relight work to cause the next frame's stall (a self-sustaining
// stutter loop). Singleplayer::tick() shrinks the budget via
// set_ingest_budget() proportionally to how many catch-up steps it expects
// to run that frame so the *frame*, not each individual step, pays this cost.

ChunkLifecycleSystem::ChunkLifecycleSystem(World &world,
		worldgen::WorldGenWorkerPool &pool, const BlockRegistry &registry) : world_(world),
																			 pool_(pool),
																			 light_(registry) {}

void ChunkLifecycleSystem::update(const std::vector<core::ChunkCoord> &desired) {
	newly_ready_.clear();
	unloaded_.clear();

	const std::unordered_set<core::ChunkCoord> wanted(desired.begin(),
			desired.end());

	auto find = [&](core::ChunkCoord c) { return world_.find_chunk(c); };
	auto ingest = [&] {
		for (auto &chunk : pool_.poll_completed()) {
			backlog_.push_back(std::move(chunk));
		}

		// Insert every finished chunk first, unlit -- relight_column() (below)
		// needs to see whatever's already in `world_` (including other chunks
		// from this same batch) to know each one's real neighbour above,
		// instead of every chunk guessing "open sky" independently. See
		// lighting.hpp's relight_column() for why: a chunk's own generation
		// order (across worker threads) has no relation to its column
		// position, so the chunk below can easily finish and need lighting
		// before the chunk above it exists yet.
		std::vector<core::ChunkCoord> just_inserted;
		std::size_t taken = 0;
		while (taken < ingest_budget_ && !backlog_.empty()) {
			std::unique_ptr<Chunk> chunk = std::move(backlog_.front());
			backlog_.pop_front();
			const core::ChunkCoord coord = chunk->coord();
			requested_.erase(coord);
			++taken;
			if (wanted.count(coord) == 0) {
				continue;
			}
			chunk->set_gen_state(GenState::kGenerated);
			world_.insert_chunk(std::move(chunk));
			just_inserted.push_back(coord);
		}
		for (core::ChunkCoord coord : just_inserted) {
			relight_column(light_, coord, find,
					[](core::ChunkCoord, const std::array<Light, kChunkVolume> &,
							const Chunk &) {});
			newly_ready_.push_back(coord);
		}
	};

	// 1. Ingest chunks finished since last update.
	ingest();

	// 2. Request wanted chunks that are neither loaded nor in flight. A saved
	//    chunk is loaded straight from disk, synchronously -- no reason to pay
	//    for a worldgen submission when its real state is already known. That
	//    synchronous load+relight work shares (1)'s ingest_budget_ instead of
	//    running unbounded: a persisted world's initial view box (e.g. ~2000
	//    chunks at view_distance=8) would otherwise load+relight *all* of them
	//    in a single update() call the first time a player (re)joins -- the
	//    same unbounded-per-tick-work failure this file's ingest_budget_
	//    comment already describes for the worldgen-finish path, just hit via
	//    the disk-load path instead (blocks the tick, and the network send
	//    that follows it, long enough that the client's loading-screen stall
	//    deadline fires over a still-empty world). Once the budget's spent
	//    this call, remaining coords are skipped entirely rather than routed
	//    to pool_.submit() -- falling through to worldgen would regenerate,
	//    and silently discard, a chunk that actually has saved data on disk.
	//    They're retried (load-checked again) on a later tick.
	std::size_t disk_loads = 0;
	for (core::ChunkCoord coord : desired) {
		if (world_.has_chunk(coord) || requested_.count(coord) != 0) {
			continue;
		}
		if (region_store_ != nullptr) {
			if (disk_loads >= ingest_budget_) {
				continue;
			}
			++disk_loads;
			if (std::unique_ptr<Chunk> loaded = region_store_->load(coord)) {
				loaded->set_gen_state(GenState::kGenerated);
				world_.insert_chunk(std::move(loaded));
				relight_column(light_, coord, find,
						[](core::ChunkCoord, const std::array<Light, kChunkVolume> &,
								const Chunk &) {});
				newly_ready_.push_back(coord);
				continue;
			}
		}
		if (pool_.submit(coord)) {
			requested_.insert(coord);
		}
	}

	// 2b. A synchronous pool finished those immediately — ingest again so the
	//     integrated server streams a chunk the same tick it's requested.
	ingest();

	// 3. Unload loaded chunks nobody wants -- save an edited one first, or its
	//    changes are lost the moment it's evicted.
	for (core::ChunkCoord coord : world_.loaded_coords()) {
		if (wanted.count(coord) == 0) {
			if (region_store_ != nullptr) {
				if (const Chunk *chunk = world_.find_chunk(coord)) {
					region_store_->save_if_dirty(*chunk);
				}
			}
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
