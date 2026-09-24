#include <doctest/doctest.h>

#include <ostream>

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "vb/net/world_replicator.hpp"
#include "vb/protocol/world.hpp"
#include "vb/world/block.hpp"
#include "vb/world/chunk.hpp"
#include "vb/world/region_store.hpp"
#include "vb/world/world.hpp"
#include "vb/worldgen/generator.hpp"
#include "vb/worldgen/worker_pool.hpp"

using namespace vb::world;
using vb::core::ChunkCoord;
using vb::core::IVec3;
using vb::core::NetId;
using vb::core::Vec3d;
namespace wg = vb::worldgen;

namespace {

std::filesystem::path temp_world_dir(const char *name) {
	auto p = std::filesystem::temp_directory_path() /
			(std::string("vb_region_store_test_") + name);
	std::filesystem::remove_all(p);
	return p;
}

// First solid voxel scanning down a column (same idiom as blockedit_test.cpp).
IVec3 surface_voxel(const BlockSolidQuery &q, int x, int z) {
	for (int y = 80; y > -16; --y) {
		if (q.solid_at({ x, y, z })) {
			return { x, y, z };
		}
	}
	return { x, 0, z };
}

} // namespace

TEST_CASE("save_if_dirty skips a never-edited chunk") {
	const auto dir = temp_world_dir("skip_unedited");
	RegionStore store(dir);
	Chunk chunk({ 1, 2, 3 }); // revision() == 0, nothing set on it
	store.save_if_dirty(chunk);
	store.flush();
	CHECK_FALSE(std::filesystem::exists(dir));

	RegionStore reopened(dir);
	CHECK_FALSE(reopened.load({ 1, 2, 3 }));
}

TEST_CASE("an edited chunk round-trips blocks through save/load") {
	const auto dir = temp_world_dir("round_trip");
	{
		RegionStore store(dir);
		Chunk chunk({ 4, -1, 7 });
		chunk.set(1, 2, 3, base_block::stone);
		chunk.set(10, 5, 10, base_block::dirt);
		store.save_if_dirty(chunk);
		store.flush();
	}

	RegionStore reopened(dir);
	auto loaded = reopened.load({ 4, -1, 7 });
	REQUIRE(loaded);
	CHECK(loaded->get(1, 2, 3) == base_block::stone);
	CHECK(loaded->get(10, 5, 10) == base_block::dirt);
	CHECK(loaded->get(0, 0, 0) == vb::core::BlockId::kAir);
	CHECK_FALSE(reopened.load({ 4, -1, 8 })); // neighbour never saved

	std::filesystem::remove_all(dir);
}

TEST_CASE("re-saving a chunk at its already-persisted revision doesn't rewrite") {
	const auto dir = temp_world_dir("no_rewrite");
	RegionStore store(dir);
	Chunk chunk({ 0, 0, 0 });
	chunk.set(0, 0, 0, base_block::stone);
	store.save_if_dirty(chunk);
	store.flush();

	const auto region_file = dir / "r.0.0.vbr";
	REQUIRE(std::filesystem::exists(region_file));
	const auto mtime_before = std::filesystem::last_write_time(region_file);

	// Same chunk, same revision -- save_if_dirty should be a no-op, so flush()
	// has nothing dirty to write and the file is left untouched.
	store.save_if_dirty(chunk);
	store.flush();
	CHECK(std::filesystem::last_write_time(region_file) == mtime_before);

	std::filesystem::remove_all(dir);
}

TEST_CASE("multiple chunks in the same region share one file") {
	const auto dir = temp_world_dir("shared_region");
	RegionStore store(dir);
	Chunk a({ 0, 0, 0 });
	a.set(0, 0, 0, base_block::stone);
	Chunk b({ 1, 0, 0 });
	b.set(0, 0, 0, base_block::dirt);
	store.save_if_dirty(a);
	store.save_if_dirty(b);
	store.flush();

	std::size_t region_files = 0;
	for (const auto &entry : std::filesystem::directory_iterator(dir)) {
		if (entry.path().extension() == ".vbr") {
			++region_files;
		}
	}
	CHECK(region_files == 1);

	RegionStore reopened(dir);
	auto la = reopened.load({ 0, 0, 0 });
	auto lb = reopened.load({ 1, 0, 0 });
	REQUIRE(la);
	REQUIRE(lb);
	CHECK(la->get(0, 0, 0) == base_block::stone);
	CHECK(lb->get(0, 0, 0) == base_block::dirt);

	std::filesystem::remove_all(dir);
}

TEST_CASE("a corrupt region file is treated as empty, not fatal") {
	const auto dir = temp_world_dir("corrupt");
	std::filesystem::create_directories(dir);
	{
		std::ofstream f(dir / "r.0.0.vbr", std::ios::binary);
		f << "not a real region file";
	}

	RegionStore store(dir);
	CHECK_FALSE(store.load({ 0, 0, 0 }));

	std::filesystem::remove_all(dir);
}

TEST_CASE("a block edit survives a real unload+reload cycle through a "
		  "RegionStore wired into ChunkLifecycleSystem via WorldReplicator") {
	const auto dir = temp_world_dir("lifecycle_integration");
	World world(BlockRegistry::base());
	wg::WorldGenWorkerPool pool(
			wg::WorldGenerator(wg::WorldGenParams{}, BlockRegistry::base()),
			wg::WorldGenWorkerPool::kSynchronous);
	RegionStore region_store(dir);
	vb::net::WorldReplicator rep(world, pool, BlockRegistry::base(),
			/*view*/ 1, /*vview*/ 2);
	rep.set_region_store(&region_store);

	const NetId id{ 1 };
	std::vector<std::pair<NetId, Vec3d>> players{ { id, { 8, 40, 8 } } };
	rep.tick(players); // streams the box around the player in

	const IVec3 target = surface_voxel(world, 8, 8);
	REQUIRE(world.solid_at(target));

	// Right above the target, well within the default 5.5-block reach.
	const Vec3d eye_at_target{
		target.x + 0.5, target.y + 2.0, target.z + 0.5
	};
	vb::protocol::C2SBlockEdit edit;
	edit.action = vb::protocol::BlockEditAction::kBreak;
	edit.pos = target;
	vb::protocol::S2CBlockEditResult result;
	rep.apply_block_edit(id, eye_at_target, edit, result);
	REQUIRE(result.accepted);
	CHECK_FALSE(world.solid_at(target));

	// Walk far enough away that every chunk around `target` leaves the box --
	// ChunkLifecycleSystem::update's unload step should save it first.
	players[0].second = Vec3d{ 8 + 32.0 * 20, 40, 8 };
	rep.tick(players);
	CHECK_FALSE(world.has_chunk(vb::core::chunk_of(target)));

	// Walk back: the chunk should be loaded from disk (with the edit intact),
	// not regenerated from scratch.
	players[0].second = Vec3d{ 8, 40, 8 };
	rep.tick(players);
	CHECK(world.get_block(target) == vb::core::BlockId::kAir);

	std::filesystem::remove_all(dir);
}
