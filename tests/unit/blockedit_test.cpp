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

// Phase 6.18 (Growtopia-style combat): ServerSession::punch() is the engine
// primitive a pack's player_input handler calls on a rising attack-key edge
// (content/base/mechanics.lua) -- these test the engine's own target
// resolution directly, with no Lua/PackRuntime involved at all, mirroring
// this file's existing plain-ServerSession style.

TEST_CASE("punch() breaks a max_damage == 0 block on the very first hit") {
	EditWorld w;
	w.server->set_player_state(w.a_id, Vec3d{ 4, 40, 4 });
	w.pump(6);

	const IVec3 target =
			surface_voxel(w.server->world_replicator()->world(), 4, 4);
	// Directly above the target, looking straight down.
	w.server->set_player_state(w.a_id,
			Vec3d{ target.x + 0.5, target.y + 3.0, target.z + 0.5 },
			vb::core::Vec2f{ 0.0f, -90.0f });
	w.pump(3);
	REQUIRE(w.server->world_replicator()->world().solid_at(target));

	const auto result = w.server->punch(w.a_id);

	CHECK(result.hit_block);
	CHECK(result.block_pos == target);
	CHECK(result.block_punches == 1);
	CHECK(result.block_broken);
	CHECK_FALSE(result.hit_player);
	CHECK_FALSE(w.server->world_replicator()->world().solid_at(target));
}

TEST_CASE("punch() accumulates hits on a max_damage > 0 block and only "
		  "breaks it once enough punches land") {
	// Needs its own registry (a custom block, not just base()) built before
	// the World/WorldReplicator that read it -- EditWorld's fields are fixed
	// to BlockRegistry::base(), so this doesn't reuse that fixture.
	LoopbackNetwork net;
	vb::world::BlockRegistry registry = vb::world::BlockRegistry::base();
	const vb::core::BlockId tough = registry.add_or_get(
			"test:tough_stone", vb::world::BlockType{
										.name = "test:tough_stone",
										.solid = true,
										.opaque = true,
										.max_damage = 3,
								});
	vb::world::World world(registry);
	wg::WorldGenWorkerPool pool(
			wg::WorldGenerator(wg::WorldGenParams{}, registry),
			wg::WorldGenWorkerPool::kSynchronous);

	HandshakeServerConfig cfg;
	cfg.world_seed = 7;
	ServerSession server(net.server(), cfg);
	server.set_world_replicator(make_rep(world, pool));
	REQUIRE(net.server().listen(0));

	Transport &ta = net.create_client();
	auto ida = ta.connect("x", 0);
	REQUIRE(ida);
	ClientSession a(ta, *ida, HandshakeClientConfig{ "A", "", "v", 1 });

	auto pump = [&](int n) {
		for (int i = 0; i < n; ++i) {
			server.tick(0.05);
			a.tick(0.05);
		}
	};
	pump(20);
	REQUIRE(a.joined());
	const NetId a_id = a.join_accept()->your_net_id;

	server.set_player_state(a_id, Vec3d{ 4, 40, 4 });
	pump(6);
	const IVec3 target = surface_voxel(world, 4, 4);
	world.set_block(target, tough); // overwrite whatever naturally generated
	server.set_player_state(a_id,
			Vec3d{ target.x + 0.5, target.y + 3.0, target.z + 0.5 },
			vb::core::Vec2f{ 0.0f, -90.0f });
	pump(3);
	REQUIRE(world.get_block(target) == tough);

	const auto r1 = server.punch(a_id);
	CHECK(r1.hit_block);
	CHECK(r1.block_punches == 1);
	CHECK_FALSE(r1.block_broken);
	CHECK(world.get_block(target) == tough); // still standing

	const auto r2 = server.punch(a_id);
	CHECK(r2.block_punches == 2);
	CHECK_FALSE(r2.block_broken);

	const auto r3 = server.punch(a_id);
	CHECK(r3.block_punches == 3);
	CHECK(r3.block_broken);
	CHECK(world.get_block(target) == vb::core::BlockId::kAir);
}

TEST_CASE("punch() self-heals an idle block's punch count back to 0 over "
		  "time, and never breaks it along the way") {
	LoopbackNetwork net;
	vb::world::BlockRegistry registry = vb::world::BlockRegistry::base();
	const vb::core::BlockId tough = registry.add_or_get(
			"test:tough_stone2", vb::world::BlockType{
										.name = "test:tough_stone2",
										.solid = true,
										.opaque = true,
										.max_damage = 5,
								});
	vb::world::World world(registry);
	wg::WorldGenWorkerPool pool(
			wg::WorldGenerator(wg::WorldGenParams{}, registry),
			wg::WorldGenWorkerPool::kSynchronous);

	HandshakeServerConfig cfg;
	cfg.world_seed = 7;
	ServerSession server(net.server(), cfg);
	// Fast heal timings so the test doesn't need hundreds of ticks to prove
	// the mechanism -- the timing values themselves are exactly what
	// vb.combat.set_params overrides in real content.
	ServerSession::PunchParams pp;
	pp.heal_after_seconds = 0.1;
	pp.heal_interval_seconds = 0.1;
	server.set_punch_params(pp);
	server.set_world_replicator(make_rep(world, pool));
	REQUIRE(net.server().listen(0));

	Transport &ta = net.create_client();
	auto ida = ta.connect("x", 0);
	REQUIRE(ida);
	ClientSession a(ta, *ida, HandshakeClientConfig{ "A", "", "v", 1 });

	auto pump = [&](int n) {
		for (int i = 0; i < n; ++i) {
			server.tick(0.05);
			a.tick(0.05);
		}
	};
	pump(20);
	REQUIRE(a.joined());
	const NetId a_id = a.join_accept()->your_net_id;

	server.set_player_state(a_id, Vec3d{ 4, 40, 4 });
	pump(6);
	const IVec3 target = surface_voxel(world, 4, 4);
	world.set_block(target, tough);
	server.set_player_state(a_id,
			Vec3d{ target.x + 0.5, target.y + 3.0, target.z + 0.5 },
			vb::core::Vec2f{ 0.0f, -90.0f });
	pump(3);

	CHECK(server.punch(a_id).block_punches == 1);
	CHECK(server.punch(a_id).block_punches == 2);

	// Idle well past heal_after_seconds + enough heal_interval_seconds
	// steps to fully repair 2 punches (0.1 + 2*0.1 = 0.3s; 1.0s of idle is
	// plenty) -- no more punches land in between.
	pump(20);

	const auto after_heal = server.punch(a_id);
	CHECK(after_heal.block_punches == 1); // healed back to 0, this is fresh
	CHECK_FALSE(after_heal.block_broken);
	CHECK(world.get_block(target) == tough);
}

TEST_CASE("punch() landing again resets the heal clock instead of stacking "
		  "idle time from before it") {
	LoopbackNetwork net;
	vb::world::BlockRegistry registry = vb::world::BlockRegistry::base();
	const vb::core::BlockId tough = registry.add_or_get(
			"test:tough_stone3", vb::world::BlockType{
										.name = "test:tough_stone3",
										.solid = true,
										.opaque = true,
										.max_damage = 5,
								});
	vb::world::World world(registry);
	wg::WorldGenWorkerPool pool(
			wg::WorldGenerator(wg::WorldGenParams{}, registry),
			wg::WorldGenWorkerPool::kSynchronous);

	HandshakeServerConfig cfg;
	cfg.world_seed = 7;
	ServerSession server(net.server(), cfg);
	ServerSession::PunchParams pp;
	pp.heal_after_seconds = 0.5;
	pp.heal_interval_seconds = 0.5;
	server.set_punch_params(pp);
	server.set_world_replicator(make_rep(world, pool));
	REQUIRE(net.server().listen(0));

	Transport &ta = net.create_client();
	auto ida = ta.connect("x", 0);
	REQUIRE(ida);
	ClientSession a(ta, *ida, HandshakeClientConfig{ "A", "", "v", 1 });

	auto pump = [&](int n) {
		for (int i = 0; i < n; ++i) {
			server.tick(0.05);
			a.tick(0.05);
		}
	};
	pump(20);
	REQUIRE(a.joined());
	const NetId a_id = a.join_accept()->your_net_id;

	server.set_player_state(a_id, Vec3d{ 4, 40, 4 });
	pump(6);
	const IVec3 target = surface_voxel(world, 4, 4);
	world.set_block(target, tough);
	server.set_player_state(a_id,
			Vec3d{ target.x + 0.5, target.y + 3.0, target.z + 0.5 },
			vb::core::Vec2f{ 0.0f, -90.0f });
	pump(3);

	CHECK(server.punch(a_id).block_punches == 1);
	pump(6); // 0.3s idle -- under heal_after_seconds (0.5s), no heal yet
	const auto r = server.punch(a_id); // resets the idle clock again
	CHECK(r.block_punches == 2); // NOT healed back to 1 first
	pump(6); // another 0.3s -- still under 0.5s since THIS punch
	CHECK(world.get_block(target) == tough);
	CHECK(server.punch(a_id).block_punches == 3); // still no heal happened
}

TEST_CASE("punch() prefers a closer player over a block further along the "
		  "same ray") {
	EditWorld w;
	// Both float at y=100, well above any generated terrain (no block along
	// the ray to compete with the player hit).
	w.server->set_player_state(
			w.a_id, Vec3d{ 0, 100, 0 }, vb::core::Vec2f{ 180.0f, 0.0f });
	w.server->set_player_state(w.b_id, Vec3d{ 0, 100, 3 });
	w.pump(3);

	const auto result = w.server->punch(w.a_id);

	CHECK(result.hit_player);
	CHECK(result.target == w.b_id);
	CHECK_FALSE(result.hit_block);
}

TEST_CASE("punch() hits nothing when no block or player is within reach") {
	EditWorld w;
	w.server->set_player_state(
			w.a_id, Vec3d{ 0, 100, 0 }, vb::core::Vec2f{ 0.0f, 0.0f });
	w.server->set_player_state(w.b_id, Vec3d{ 500, 100, 500 }); // far away
	w.pump(3);

	const auto result = w.server->punch(w.a_id);

	CHECK_FALSE(result.hit_player);
	CHECK_FALSE(result.hit_block);
}
