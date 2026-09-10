#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_set>
#include <vector>

#include "vb/core/ids.hpp"
#include "vb/world/chunk.hpp"
#include "vb/worldgen/generator.hpp"

// Worldgen worker pool (spec §13). Chunk generation is pure and deterministic,
// so it runs on N background threads; finished chunks are handed back to the
// tick thread via poll_completed(). The generator is shared read-only.

namespace vb::worldgen {

class WorldGenWorkerPool {
public:
	// threads == 0 -> pick from hardware_concurrency. A synchronous pool
	// (threads with kSynchronous) generates inside submit() with no background
	// threads — deterministic, for tests and the integrated server.
	static constexpr std::size_t kSynchronous = static_cast<std::size_t>(-1);
	WorldGenWorkerPool(WorldGenerator generator, std::size_t threads = 0);
	~WorldGenWorkerPool();

	WorldGenWorkerPool(const WorldGenWorkerPool &) = delete;
	WorldGenWorkerPool &operator=(const WorldGenWorkerPool &) = delete;

	// Queue a chunk for generation. Duplicate coords already queued or in
	// flight are ignored. Returns false if it was a duplicate.
	bool submit(core::ChunkCoord coord);

	// Move all finished chunks out (tick thread). Never blocks.
	std::vector<std::unique_ptr<world::Chunk>> poll_completed();

	std::size_t pending() const;
	std::size_t thread_count() const { return workers_.size(); }

private:
	void worker_loop();

	WorldGenerator generator_;

	mutable std::mutex mutex_;
	std::condition_variable cv_;
	std::deque<core::ChunkCoord> queue_;
	std::unordered_set<core::ChunkCoord> in_flight_;
	std::vector<std::unique_ptr<world::Chunk>> completed_;
	std::atomic_bool stop_{ false };
	bool synchronous_ = false;
	std::vector<std::thread> workers_;
};

} // namespace vb::worldgen
