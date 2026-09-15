#include <doctest/doctest.h>

#include <ostream>

#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "vb/net/loopback.hpp"
#include "vb/net/session.hpp"
#include "vb/net/world_replicator.hpp"
#include "vb/world/block.hpp"
#include "vb/world/chunk.hpp"
#include "vb/world/paletted_chunk_store.hpp"
#include "vb/world/world.hpp"
#include "vb/worldgen/generator.hpp"
#include "vb/worldgen/worker_pool.hpp"

using namespace vb::net;
using vb::core::NetId;
using vb::core::Vec3d;
namespace wg = vb::worldgen;

namespace {

std::unique_ptr<WorldReplicator> make_replicator(vb::world::World &world,
		wg::WorldGenWorkerPool &pool, int view, int vview) {
	return std::make_unique<WorldReplicator>(world, pool,
			vb::world::BlockRegistry::base(), view, vview);
}

} // namespace

TEST_CASE("WorldReplicator streams chunks around a player and reclaims them") {
	vb::world::World world(vb::world::BlockRegistry::base());
	wg::WorldGenWorkerPool pool(
			wg::WorldGenerator(wg::WorldGenParams{}, vb::world::BlockRegistry::base()),
			wg::WorldGenWorkerPool::kSynchronous);
	WorldReplicator rep(world, pool, vb::world::BlockRegistry::base(),
			/*view*/ 1, /*vview*/ 1);

	std::vector<std::pair<NetId, Vec3d>> players{ { NetId{ 1 }, { 8, 40, 8 } } };

	auto f1 = rep.tick(players); // generates + sends the 3x3x3 box
	REQUIRE(f1.size() == 1);
	CHECK(f1[0].id == NetId{ 1 });
	CHECK(f1[0].frames.size() == 27); // 3*3*3 chunk adds
	CHECK(world.chunk_count() == 27);

	auto f2 = rep.tick(players); // nothing new
	CHECK(f2.empty());

	// Walk far in +x: the box shifts, some leave and some enter.
	players[0].second = Vec3d{ 8 + 32.0 * 5, 40, 8 };
	auto f3 = rep.tick(players);
	REQUIRE(f3.size() == 1);
	std::size_t adds = 0;
	std::size_t removes = 0;
	for (const auto &fr : f3[0].frames) {
		std::size_t consumed = 0;
		auto parsed = vb::protocol::read_frame({ fr.bytes.data(), fr.bytes.size() },
				consumed);
		REQUIRE(parsed);
		if (parsed->header.type == vb::protocol::MessageType::kS2CChunkAdd) {
			++adds;
		} else if (parsed->header.type ==
				vb::protocol::MessageType::kS2CChunkRemove) {
			++removes;
		}
	}
	CHECK(adds == 27); // whole new box (moved > 2*view away)
	CHECK(removes == 27); // whole old box gone

	rep.forget_player(NetId{ 1 });
}

TEST_CASE("integrated: a joined client mirrors the chunks around its spawn") {
	LoopbackNetwork net;

	vb::world::World world(vb::world::BlockRegistry::base());
	wg::WorldGenWorkerPool pool(
			wg::WorldGenerator(wg::WorldGenParams{}, vb::world::BlockRegistry::base()),
			wg::WorldGenWorkerPool::kSynchronous);

	HandshakeServerConfig cfg;
	cfg.world_seed = 7;
	ServerSession server(net.server(), cfg);
	server.set_world_replicator(make_replicator(world, pool, 1, 1));
	REQUIRE(net.server().listen(0));

	Transport &ct = net.create_client();
	auto id = ct.connect("x", 0);
	REQUIRE(id);
	ClientSession client(ct, *id, HandshakeClientConfig{ "Spawner", "", "v", 1 });

	auto pump = [&](int n) {
		for (int i = 0; i < n; ++i) {
			server.tick(0.05);
			client.tick(0.05);
		}
	};

	pump(16);
	REQUIRE(client.joined());
	const NetId me = client.join_accept()->your_net_id;

	server.set_player_state(me, Vec3d{ 4, 40, 4 });
	pump(4);

	CHECK(client.chunk_store().size() == 27);
	CHECK(client.chunk_store().has({ 0, 1, 0 }));
	// The mirrored chunk is queryable as solid terrain well below the surface.
	CHECK(client.chunk_store().solid_at({ 4, 5, 4 }));

	// Move the player far; the client should drop the old chunks.
	server.set_player_state(me, Vec3d{ 4 + 32.0 * 6, 40, 4 });
	pump(4);
	CHECK_FALSE(client.chunk_store().has({ 0, 1, 0 }));
	CHECK(client.chunk_store().size() == 27);
}

// Regression: mining deep enough to cross a chunk boundary (every 32 blocks
// vertically) showed a false-bright block right where it was broken. This
// generator has no caves (WorldGenerator fills solid below its heightmap
// unconditionally, see generator.cpp), so underground open space only ever
// exists where a player has actually mined it -- the exact trigger here is
// breaking a block at the very top of a chunk (local y = 31) that's buried
// under another solid, loaded chunk above it. The old apply_block_edit
// called relight_chunk() with no `above` argument, i.e. always assumed open
// sky at that exact spot.
TEST_CASE("breaking a block at the top of a buried chunk stays dark, not "
		"falsely sky-lit") {
	vb::world::World world(vb::world::BlockRegistry::base());
	wg::WorldGenWorkerPool pool(
			wg::WorldGenerator(wg::WorldGenParams{}, vb::world::BlockRegistry::base()),
			wg::WorldGenWorkerPool::kSynchronous);
	WorldReplicator rep(world, pool, vb::world::BlockRegistry::base(),
			/*view*/ 0, /*vview*/ 1);

	// Deep underground: base_height=64, amplitude=28 (WorldGenParams{}
	// defaults) means the real surface never dips below world y=36 for any
	// seed, so every chunk with a top at world y <= 31 (chunk y <= 0) is
	// guaranteed 100% solid stone. Centering the view on chunk y=-1 with
	// vview=1 loads chunks y=-2, -1, 0 -- all solid, all in one synchronous
	// tick (also covers ChunkLifecycleSystem's "generated together in the
	// same batch" cascade path).
	std::vector<std::pair<NetId, Vec3d>> players{ { NetId{ 1 }, { 4, -16, 4 } } };
	rep.tick(players);
	REQUIRE(world.chunk_count() == 3);
	REQUIRE(world.find_chunk({ 0, 0, 0 }) != nullptr); // the "roof" above the break

	using vb::world::kChunkDim;
	const vb::core::IVec3 target{ 5, -1, 5 }; // local (5, 31, 5) of chunk {0,-1,0}
	REQUIRE(world.get_block(target) == vb::world::base_block::stone);

	vb::protocol::C2SBlockEdit edit;
	edit.predicted_seq = 1;
	edit.action = vb::protocol::BlockEditAction::kBreak;
	edit.pos = target;
	vb::protocol::S2CBlockEditResult result;
	const Vec3d eye{ 5.5, -0.5, 5.5 }; // right at the target voxel's centre
	rep.apply_block_edit(NetId{ 1 }, eye, edit, result);
	REQUIRE(result.accepted);

	const vb::world::Chunk *mid = world.find_chunk({ 0, -1, 0 });
	REQUIRE(mid != nullptr);
	CHECK(mid->light(5, kChunkDim - 1, 5).sky() == 0);
}

// Regression: a chunk lit assuming open sky (nothing loaded above it yet,
// generated on a background worker thread before its real neighbour) can be
// sent to a player, then have that guess corrected server-side once its
// neighbour above finishes loading -- arbitrarily many ticks later. Without
// tracking the revision most recently sent per (player, chunk),
// WorldReplicator::tick() only notices chunks entering/leaving a player's
// view, never an already-visible chunk changing in place, so the correction
// silently never reached an already-connected player.
TEST_CASE("tick() re-sends an already-visible chunk whose revision changes "
		"in place") {
	vb::world::World world(vb::world::BlockRegistry::base());
	wg::WorldGenWorkerPool pool(
			wg::WorldGenerator(wg::WorldGenParams{}, vb::world::BlockRegistry::base()),
			wg::WorldGenWorkerPool::kSynchronous);
	WorldReplicator rep(world, pool, vb::world::BlockRegistry::base(),
			/*view*/ 0, /*vview*/ 0); // 1x1x1: just the player's own chunk

	std::vector<std::pair<NetId, Vec3d>> players{ { NetId{ 1 }, { 4, 40, 4 } } };
	auto f1 = rep.tick(players);
	REQUIRE(f1.size() == 1);
	REQUIRE(f1[0].frames.size() == 1); // the initial S2C_ChunkAdd

	auto f2 = rep.tick(players); // nothing changed: no re-send
	CHECK(f2.empty());

	// Simulate the chunk's data changing server-side without going through
	// apply_block_edit -- exactly what a lighting::relight_column() cascade
	// does when a later-arriving neighbour corrects this chunk's light.
	vb::world::Chunk *chunk = world.find_chunk(vb::core::chunk_of({ 4, 40, 4 }));
	REQUIRE(chunk != nullptr);
	chunk->bump_revision();

	auto f3 = rep.tick(players);
	REQUIRE(f3.size() == 1);
	REQUIRE(f3[0].frames.size() == 1);
	std::size_t consumed = 0;
	auto parsed = vb::protocol::read_frame(
			{ f3[0].frames[0].bytes.data(), f3[0].frames[0].bytes.size() }, consumed);
	REQUIRE(parsed);
	CHECK(parsed->header.type == vb::protocol::MessageType::kS2CChunkAdd);
}
