#include <doctest/doctest.h>

#include <ostream>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <random>
#include <string>
#include <vector>

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

// ARCHITECTURE_SPEC.md's Testing Strategy describes a soak as "N simulated
// clients doing random walks + edits for M minutes, watch for leaks
// (ASan/LSan) and unbounded queue growth." This is that soak, scaled to run
// inside the normal (deterministic, no real sleeps) vb_tests suite rather
// than as a separate scheduled job: WorldGenWorkerPool::kSynchronous keeps
// worldgen single-threaded and every tick advances simulated time directly
// via ServerSession::tick()/ClientSession::tick() (the same "pump" pattern
// blockedit_test.cpp/replication_test.cpp already use). A fixed-seed
// std::mt19937 keeps a soak failure reproducible instead of a flaky
// one-in-N flake. Real ASan/LSan leak detection comes for free from this
// test simply being part of vb_tests, which the Linux CI matrix already
// runs under -DVB_ENABLE_ASAN (added earlier this same day) -- no separate
// nightly job was added for that half of the spec's description, only for
// the "watch queue growth" half, which needs no sanitizer to check.
// **Kept deliberately small** (4 clients, 150 ticks, clients kept close
// together): real fBm terrain generation, not the sim logic being soaked,
// dominates this test's cost in an unoptimized Debug build -- an earlier,
// larger version of this test (6 clients spread far enough apart to each
// force a distinct set of chunk columns, 400 ticks) measured ~55s of real
// CPU time on its own; this version measured ~14s standalone against a
// ~70s baseline for the other 412 tests combined, a proportionate addition
// rather than a suite-doubling one.
namespace {

constexpr int kClientCount = 4;
constexpr int kSoakTicks = 150;

struct SoakClient {
	Transport *transport;
	ConnId conn;
	std::unique_ptr<ClientSession> session;
	NetId id{ 0 };
	Vec3d pos;
	std::uint32_t next_edit_seq = 1;
};

} // namespace

TEST_CASE("soak: N simulated clients random-walk + edit for many ticks with "
		"no unbounded queue growth and no leaked connection on disconnect") {
	LoopbackNetwork net;
	vb::world::World world{ vb::world::BlockRegistry::base() };
	wg::WorldGenWorkerPool pool{ wg::WorldGenerator(wg::WorldGenParams{},
										 vb::world::BlockRegistry::base()),
		wg::WorldGenWorkerPool::kSynchronous };

	HandshakeServerConfig cfg;
	cfg.world_seed = 99;
	ServerSession server(net.server(), cfg);
	server.set_world_replicator(std::make_unique<WorldReplicator>(
			world, pool, vb::world::BlockRegistry::base(), /*view*/ 1, /*vview*/ 2));
	REQUIRE(net.server().listen(0));

	std::mt19937 rng(1234); // fixed seed -- a soak failure must stay reproducible
	std::uniform_real_distribution<double> step(-2.0, 2.0);
	std::uniform_int_distribution<int> edit_chance(0, 4); // ~1-in-5 ticks per client

	std::vector<std::unique_ptr<SoakClient>> clients;
	for (int i = 0; i < kClientCount; ++i) {
		Transport &t = net.create_client();
		auto conn = t.connect("x", 0);
		REQUIRE(conn);
		auto c = std::make_unique<SoakClient>();
		c->transport = &t;
		c->conn = *conn;
		c->session = std::make_unique<ClientSession>(t, *conn,
				HandshakeClientConfig{ "soak" + std::to_string(i), "", "v", 1 });
		// Spread clients out a little so their interest sets aren't all
		// perfectly identical, but not so far apart that each one needs its
		// own brand-new set of chunk columns -- worldgen cost (real fBm
		// terrain, unoptimized in a Debug build) dominates this test's
		// runtime far more than the sim logic being soaked, so keeping
		// everyone inside a small shared area is what keeps this test fast
		// enough to belong in the default suite while still exercising N
		// concurrent connections instead of just one.
		c->pos = Vec3d{ static_cast<double>(i) * 8.0, 40.0, 0.0 };
		clients.push_back(std::move(c));
	}

	auto pump_all = [&](int rounds) {
		for (int r = 0; r < rounds; ++r) {
			server.tick(0.05);
			for (auto &c : clients) {
				c->session->tick(0.05);
			}
		}
	};

	pump_all(20);
	for (auto &c : clients) {
		REQUIRE(c->session->joined());
		c->id = c->session->join_accept()->your_net_id;
	}

	std::size_t max_loaded_chunks = 0;
	std::size_t max_pending_edits = 0;
	std::size_t max_unacked_inputs = 0;

	for (int tick = 0; tick < kSoakTicks; ++tick) {
		for (auto &c : clients) {
			c->pos.x += step(rng);
			c->pos.z += step(rng);
			server.set_player_state(c->id, c->pos);

			if (edit_chance(rng) == 0) {
				vb::protocol::C2SBlockEdit e;
				e.predicted_seq = c->next_edit_seq++;
				e.action = (edit_chance(rng) % 2 == 0)
						? vb::protocol::BlockEditAction::kBreak
						: vb::protocol::BlockEditAction::kPlace;
				e.pos = { static_cast<int>(c->pos.x), static_cast<int>(c->pos.y) - 1,
					static_cast<int>(c->pos.z) };
				e.block = vb::world::base_block::stone;
				c->session->push_block_edit(e);
			}
		}

		server.tick(0.05);
		for (auto &c : clients) {
			c->session->tick(0.05);
			max_pending_edits =
					std::max(max_pending_edits, c->session->pending_edit_count());
			max_unacked_inputs =
					std::max(max_unacked_inputs, c->session->unacked_input_count());
		}
		max_loaded_chunks = std::max(max_loaded_chunks, world.loaded_coords().size());
	}

	// A real leak/crash/stuck-connection bug under sustained load would show
	// up here as a dropped or never-completed join, not just as a silently
	// slow one.
	CHECK(server.player_count() == static_cast<std::size_t>(kClientCount));
	CHECK(server.pending_count() == 0);

	// Bounded, not merely "small": the exact numbers depend on view
	// distance/interest radius, but a real queue-growth bug (an ack that
	// stops being sent, an edit result that never arrives) would blow these
	// far past a generous multiple of what a single tick's worth of new
	// work could ever add across kSoakTicks.
	CHECK(max_pending_edits < 50);
	CHECK(max_unacked_inputs < 50);
	// view=1 -> each player only ever needs a small local column of chunks
	// as they random-walk a bounded area; if chunk unload ever stopped
	// working, this would instead grow roughly linearly with kSoakTicks.
	CHECK(max_loaded_chunks < 400);

	for (auto &c : clients) {
		CHECK(c->session->pending_edit_count() == 0); // caught up by the end
	}

	// Disconnect every client and confirm the server actually reclaims each
	// connection's state, rather than leaking a Conn entry per soak client
	// that ever joined.
	for (auto &c : clients) {
		c->transport->close(c->conn, "soak test done");
	}
	pump_all(5);
	CHECK(server.player_count() == 0);
	CHECK(server.pending_count() == 0);
}
