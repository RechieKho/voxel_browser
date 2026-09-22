#include "vb/world/chunk_mesh_worker_pool.hpp"

#include <algorithm>
#include <utility>

namespace vb::world {

ChunkMeshWorkerPool::ChunkMeshWorkerPool(std::size_t threads) {
	if (threads == kSynchronous) {
		synchronous_ = true;
		return;
	}
	if (threads == 0) {
		// Half of hardware_concurrency, not hw-1 like WorldGenWorkerPool: the
		// integrated singleplayer path runs both pools in the same process, so
		// a matching hw-1 default here would oversubscribe every core by
		// roughly 2x on top of the main/render/net threads. Meshing bursts
		// also tend to overlap worldgen bursts (both triggered by the same
		// player movement), so leaving headroom for the other pool matters
		// more here than for a server-only pool.
		const unsigned hw = std::thread::hardware_concurrency();
		threads = hw > 2 ? static_cast<std::size_t>(hw / 2) : 1;
	}
	workers_.reserve(threads);
	for (std::size_t i = 0; i < threads; ++i) {
		workers_.emplace_back([this] { worker_loop(); });
	}
}

ChunkMeshWorkerPool::~ChunkMeshWorkerPool() {
	stop_.store(true, std::memory_order_relaxed);
	cv_.notify_all();
	for (std::thread &t : workers_) {
		if (t.joinable()) {
			t.join();
		}
	}
}

ChunkMeshResult ChunkMeshWorkerPool::mesh(const Job &job) {
	ChunkMeshResult result;
	result.coord = job.snapshot.coord;
	result.revision = job.snapshot.revision;
	result.mesh = mesh_chunk_from_snapshot(job.snapshot, job.registry);
	return result;
}

bool ChunkMeshWorkerPool::submit(ChunkMeshSnapshot snapshot, BlockRegistry registry) {
	const core::ChunkCoord coord = snapshot.coord;
	{
		std::lock_guard lock(mutex_);
		if (in_flight_.count(coord) != 0) {
			return false;
		}
		if (std::find_if(queue_.begin(), queue_.end(), [&](const Job &j) {
				return j.snapshot.coord == coord;
			}) != queue_.end()) {
			return false;
		}
		if (synchronous_) {
			completed_.push_back(mesh(Job{ std::move(snapshot), std::move(registry) }));
			return true;
		}
		queue_.push_back(Job{ std::move(snapshot), std::move(registry) });
	}
	cv_.notify_one();
	return true;
}

bool ChunkMeshWorkerPool::in_flight_or_queued(core::ChunkCoord coord) const {
	std::lock_guard lock(mutex_);
	if (in_flight_.count(coord) != 0) {
		return true;
	}
	return std::find_if(queue_.begin(), queue_.end(), [&](const Job &j) {
		return j.snapshot.coord == coord;
	}) != queue_.end();
}

std::vector<ChunkMeshResult> ChunkMeshWorkerPool::poll_completed() {
	std::lock_guard lock(mutex_);
	return std::exchange(completed_, {});
}

std::size_t ChunkMeshWorkerPool::pending() const {
	std::lock_guard lock(mutex_);
	return queue_.size() + in_flight_.size();
}

void ChunkMeshWorkerPool::worker_loop() {
	for (;;) {
		Job job;
		{
			std::unique_lock lock(mutex_);
			cv_.wait(lock, [this] {
				return stop_.load(std::memory_order_relaxed) || !queue_.empty();
			});
			if (stop_.load(std::memory_order_relaxed)) {
				return;
			}
			job = std::move(queue_.front());
			queue_.pop_front();
			in_flight_.insert(job.snapshot.coord);
		}

		ChunkMeshResult result = mesh(job);

		{
			std::lock_guard lock(mutex_);
			in_flight_.erase(result.coord);
			completed_.push_back(std::move(result));
		}
	}
}

} // namespace vb::world
