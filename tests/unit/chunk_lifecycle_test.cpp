#include <doctest/doctest.h>

#include <ostream>
#include <vector>

#include "vb/world/chunk_lifecycle.hpp"
#include "vb/world/world.hpp"
#include "vb/worldgen/generator.hpp"
#include "vb/worldgen/worker_pool.hpp"

namespace wg = vb::worldgen;
using vb::core::ChunkCoord;
using vb::world::ChunkLifecycleSystem;

namespace {

std::vector<ChunkCoord> box(int r, int h) {
	std::vector<ChunkCoord> out;
	for (int y = -h; y <= h; ++y) {
		for (int z = -r; z <= r; ++z) {
			for (int x = -r; x <= r; ++x) {
				out.push_back({ x, y, z });
			}
		}
	}
	return out;
}

} // namespace

// Lighting newly generated chunks is the costly part of a tick while a view
// box fills; the time budget bounds it per update() but never to zero.
TEST_CASE("chunk lifecycle: the ingest time budget paces work, one batch per update minimum") {
	const auto registry = vb::world::BlockRegistry::base();
	vb::world::World world(registry);
	wg::WorldGenWorkerPool pool(wg::WorldGenerator(wg::WorldGenParams{}, registry),
			wg::WorldGenWorkerPool::kSynchronous);
	ChunkLifecycleSystem lifecycle(world, pool, registry);
	lifecycle.set_ingest_time_budget(1e-6); // spent by the first batch
	const auto desired = box(2, 1); // 75 chunks

	lifecycle.update(desired);
	CHECK(world.chunk_count() == ChunkLifecycleSystem::kIngestBatch);
	CHECK(lifecycle.newly_ready().size() == ChunkLifecycleSystem::kIngestBatch);
	lifecycle.update(desired);
	CHECK(world.chunk_count() == 2 * ChunkLifecycleSystem::kIngestBatch);

	// Without a time budget the chunk-count budget alone applies.
	lifecycle.set_ingest_time_budget(0.0);
	lifecycle.update(desired);
	CHECK(world.chunk_count() > 3 * ChunkLifecycleSystem::kIngestBatch);
	for (int i = 0; i < 10 && world.chunk_count() < desired.size(); ++i) {
		lifecycle.update(desired);
	}
	CHECK(world.chunk_count() == desired.size());
}
