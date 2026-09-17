#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <thread>
#include <unordered_set>
#include <vector>

#include "vb/core/ids.hpp"
#include "vb/world/block.hpp"
#include "vb/world/chunk_mesh_snapshot.hpp"
#include "vb/world/chunk_mesher.hpp"

// Chunk meshing worker pool. mesh_chunk_from_snapshot() is a pure function of
// a ChunkMeshSnapshot, so -- like WorldGenWorkerPool -- it can run on
// background threads: ChunkRenderer builds a snapshot on the main thread (the
// only thread allowed to touch ClientChunkStore), submits it here, and later
// drains poll_completed() to GPU-upload finished meshes (also main-thread
// only, a raylib/GL requirement).

namespace vb::world {

struct ChunkMeshResult {
	core::ChunkCoord coord{};
	std::uint64_t revision = 0; // the chunk revision the snapshot was built from
	MeshData mesh;
};

class ChunkMeshWorkerPool {
public:
	// threads == 0 -> pick from hardware_concurrency (see the .cpp for the
	// split rationale vs. WorldGenWorkerPool). kSynchronous meshes inside
	// submit() with no background threads -- deterministic, for tests.
	static constexpr std::size_t kSynchronous = static_cast<std::size_t>(-1);
	explicit ChunkMeshWorkerPool(std::size_t threads = 0);
	~ChunkMeshWorkerPool();

	ChunkMeshWorkerPool(const ChunkMeshWorkerPool &) = delete;
	ChunkMeshWorkerPool &operator=(const ChunkMeshWorkerPool &) = delete;

	// Queue a mesh job. Duplicate coords already queued or in-flight are
	// ignored (returns false) -- the caller resubmits once the earlier job's
	// result is drained via poll_completed() (a stale-revision result should
	// be discarded by the caller, see ChunkRenderer::sync()).
	bool submit(ChunkMeshSnapshot snapshot, BlockRegistry registry);

	// True if `coord` already has a job queued or in flight -- lets a caller
	// skip building a redundant snapshot before calling submit().
	bool in_flight_or_queued(core::ChunkCoord coord) const;

	// Move all finished results out (main thread). Never blocks.
	std::vector<ChunkMeshResult> poll_completed();

	std::size_t pending() const;
	std::size_t thread_count() const { return workers_.size(); }

private:
	struct Job {
		ChunkMeshSnapshot snapshot;
		BlockRegistry registry;
	};

	void worker_loop();
	static ChunkMeshResult mesh(const Job &job);

	mutable std::mutex mutex_;
	std::condition_variable cv_;
	std::deque<Job> queue_;
	std::unordered_set<core::ChunkCoord> in_flight_;
	std::vector<ChunkMeshResult> completed_;
	std::atomic_bool stop_{ false };
	bool synchronous_ = false;
	std::vector<std::thread> workers_;
};

} // namespace vb::world
