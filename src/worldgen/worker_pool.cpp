#include "vb/worldgen/worker_pool.hpp"

#include <algorithm>
#include <utility>

namespace vb::worldgen {

WorldGenWorkerPool::WorldGenWorkerPool(WorldGenerator generator,
		std::size_t threads) : generator_(std::move(generator)) {
	if (threads == kSynchronous) {
		synchronous_ = true;
		return;
	}
	if (threads == 0) {
		const unsigned hw = std::thread::hardware_concurrency();
		threads = hw > 2 ? static_cast<std::size_t>(hw - 1) : 1;
	}
	workers_.reserve(threads);
	for (std::size_t i = 0; i < threads; ++i) {
		workers_.emplace_back([this] { worker_loop(); });
	}
}

WorldGenWorkerPool::~WorldGenWorkerPool() {
	stop_.store(true, std::memory_order_relaxed);
	cv_.notify_all();
	for (std::thread &t : workers_) {
		if (t.joinable()) {
			t.join();
		}
	}
}

bool WorldGenWorkerPool::submit(core::ChunkCoord coord) {
	{
		std::lock_guard lock(mutex_);
		if (in_flight_.count(coord) != 0) {
			return false;
		}
		if (std::find(queue_.begin(), queue_.end(), coord) != queue_.end()) {
			return false;
		}
		if (synchronous_) {
			auto chunk = std::make_unique<world::Chunk>(coord);
			generator_.generate(*chunk);
			completed_.push_back(std::move(chunk));
			return true;
		}
		queue_.push_back(coord);
	}
	cv_.notify_one();
	return true;
}

std::vector<std::unique_ptr<world::Chunk>>
WorldGenWorkerPool::poll_completed() {
	std::lock_guard lock(mutex_);
	return std::exchange(completed_, {});
}

std::size_t WorldGenWorkerPool::pending() const {
	std::lock_guard lock(mutex_);
	return queue_.size() + in_flight_.size();
}

void WorldGenWorkerPool::worker_loop() {
	for (;;) {
		core::ChunkCoord coord{};
		{
			std::unique_lock lock(mutex_);
			cv_.wait(lock, [this] {
				return stop_.load(std::memory_order_relaxed) || !queue_.empty();
			});
			if (stop_.load(std::memory_order_relaxed)) {
				return;
			}
			coord = queue_.front();
			queue_.pop_front();
			in_flight_.insert(coord);
		}

		auto chunk = std::make_unique<world::Chunk>(coord);
		generator_.generate(*chunk);

		{
			std::lock_guard lock(mutex_);
			in_flight_.erase(coord);
			completed_.push_back(std::move(chunk));
		}
	}
}

} // namespace vb::worldgen
