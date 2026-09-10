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
