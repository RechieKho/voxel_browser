#include <doctest/doctest.h>

#include <ostream>

#include <memory>
#include <optional>

#include "vb/net/loopback.hpp"
#include "vb/net/session.hpp"
#include "vb/net/world_replicator.hpp"
#include "vb/protocol/world.hpp"
#include "vb/world/block.hpp"
#include "vb/world/world.hpp"
#include "vb/worldgen/generator.hpp"
#include "vb/worldgen/worker_pool.hpp"

using namespace vb::net;
using vb::core::IVec3;
using vb::core::NetId;
using vb::core::Vec3d;
namespace wg = vb::worldgen;

namespace {

std::unique_ptr<WorldReplicator> make_rep(vb::world::World &w,
		wg::WorldGenWorkerPool &pool) {
	return std::make_unique<WorldReplicator>(w, pool,
			vb::world::BlockRegistry::base(), /*view*/ 1, /*vview*/ 2);
}

// First solid voxel scanning down a column.
IVec3 surface_voxel(const vb::world::BlockSolidQuery &q, int x, int z) {
	for (int y = 80; y > -16; --y) {
		if (q.solid_at({ x, y, z })) {
			return { x, y, z };
		}
	}
	return { x, 0, z };
}

} // namespace

TEST_CASE("C2SBlockEdit / S2CBlockEditResult round-trip") {
	vb::protocol::C2SBlockEdit e;
	e.predicted_seq = 42;
	e.action = vb::protocol::BlockEditAction::kPlace;
	e.pos = { -3, 70, 128 };
	e.block = static_cast<vb::core::BlockId>(6);
	std::vector<std::byte> b;
	e.encode(b);
	auto d = vb::protocol::C2SBlockEdit::decode({ b.data(), b.size() });
	REQUIRE(d);
	CHECK(d->predicted_seq == 42);
	CHECK(d->action == vb::protocol::BlockEditAction::kPlace);
	CHECK(d->pos == IVec3{ -3, 70, 128 });
	CHECK(d->block == static_cast<vb::core::BlockId>(6));

	vb::protocol::S2CBlockEditResult r;
	r.predicted_seq = 42;
	r.accepted = true;
	r.pos = e.pos;
	std::vector<std::byte> rb;
	r.encode(rb);
	auto rd = vb::protocol::S2CBlockEditResult::decode({ rb.data(), rb.size() });
	REQUIRE(rd);
	CHECK(rd->accepted);
	CHECK(rd->pos == IVec3{ -3, 70, 128 });
}

namespace {

struct EditWorld {
	LoopbackNetwork net;
	vb::world::World world{ vb::world::BlockRegistry::base() };
	wg::WorldGenWorkerPool pool{ wg::WorldGenerator(wg::WorldGenParams{},
										 vb::world::BlockRegistry::base()),
		wg::WorldGenWorkerPool::kSynchronous };
	std::optional<ServerSession> server;
	std::optional<ClientSession> a;
	std::optional<ClientSession> b;
	NetId a_id{ 0 };
	NetId b_id{ 0 };

	EditWorld() {
		HandshakeServerConfig cfg;
		cfg.world_seed = 7;
		server.emplace(net.server(), cfg);
		server->set_world_replicator(make_rep(world, pool));
		REQUIRE(net.server().listen(0));

		Transport &ta = net.create_client();
		auto ida = ta.connect("x", 0);
		REQUIRE(ida);
		a.emplace(ta, *ida, HandshakeClientConfig{ "A", "", "v", 1 });
		Transport &tb = net.create_client();
		auto idb = tb.connect("x", 0);
		REQUIRE(idb);
		b.emplace(tb, *idb, HandshakeClientConfig{ "B", "", "v", 2 });

		pump(20);
		REQUIRE(a->joined());
		REQUIRE(b->joined());
		a_id = a->join_accept()->your_net_id;
		b_id = b->join_accept()->your_net_id;
	}

	void pump(int n) {
		for (int i = 0; i < n; ++i) {
			server->tick(0.05);
			a->tick(0.05);
			b->tick(0.05);
		}
	}
};

} // namespace

TEST_CASE("a break edit applies on the server and fans out to both clients") {
	EditWorld w;
	// Both players stand near the origin so the target is in range + streamed.
	w.server->set_player_state(w.a_id, Vec3d{ 4, 40, 4 });
	w.server->set_player_state(w.b_id, Vec3d{ 6, 40, 4 });
	w.pump(6);

	const IVec3 target =
			surface_voxel(w.server->world_replicator()->world(), 4, 4);
	// Put A's eye right above the target, well within reach.
	w.server->set_player_state(
			w.a_id, Vec3d{ target.x + 0.5, target.y + 2.0, target.z + 0.5 });
	w.pump(3);

	REQUIRE(w.a->chunk_store().solid_at(target));

	vb::protocol::C2SBlockEdit e;
	e.predicted_seq = 1;
	e.action = vb::protocol::BlockEditAction::kBreak;
	e.pos = target;
	w.a->push_block_edit(e);
	CHECK(w.a->chunk_store().block_at(target) == vb::core::BlockId::kAir); // optimistic

	w.pump(6);

	CHECK_FALSE(w.server->world_replicator()->world().solid_at(target));
	CHECK(w.a->chunk_store().block_at(target) == vb::core::BlockId::kAir);
	CHECK(w.b->chunk_store().block_at(target) == vb::core::BlockId::kAir);
	CHECK(w.a->pending_edit_count() == 0);
}

TEST_CASE("an out-of-reach edit is rejected and rolled back on the client") {
	EditWorld w;
	w.server->set_player_state(w.a_id, Vec3d{ 4, 40, 4 });
	w.pump(6);

	const IVec3 target =
			surface_voxel(w.server->world_replicator()->world(), 4, 4);
	// A is far above -> outside the 5.5 block reach.
	w.server->set_player_state(w.a_id, Vec3d{ 4, 90, 4 });
	w.pump(3);

	const vb::core::BlockId before = w.a->chunk_store().block_at(target);
	REQUIRE(before != vb::core::BlockId::kAir);

	vb::protocol::C2SBlockEdit e;
	e.predicted_seq = 7;
	e.action = vb::protocol::BlockEditAction::kBreak;
	e.pos = target;
	w.a->push_block_edit(e);
	CHECK(w.a->chunk_store().block_at(target) == vb::core::BlockId::kAir);
	CHECK(w.a->pending_edit_count() == 1);

	w.pump(6);

	CHECK(w.server->world_replicator()->world().solid_at(target));
	CHECK(w.a->chunk_store().block_at(target) == before); // rolled back
	CHECK(w.a->pending_edit_count() == 0);
}
