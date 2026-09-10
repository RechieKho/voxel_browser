#include <doctest/doctest.h>

#include <ostream>

#include <optional>
#include <vector>

#include "vb/net/loopback.hpp"
#include "vb/net/session.hpp"
#include "vb/replication/interest.hpp"

using namespace vb::net;
using vb::core::NetId;
using vb::core::Vec3d;

// --- InterestGrid unit tests ----------------------------------------------

TEST_CASE("InterestGrid: Chebyshev cell radius, self excluded, sorted") {
	vb::replication::InterestGrid g(10.0);
	g.upsert({ NetId{ 1 }, {}, { 0, 0, 0 }, {}, {} });
	g.upsert({ NetId{ 2 }, {}, { 5, 0, 0 }, {}, {} }); // same cell as 1
	g.upsert({ NetId{ 3 }, {}, { 25, 0, 0 }, {}, {} }); // 2 cells away
	g.upsert({ NetId{ 4 }, {}, { 0, 0, 100 }, {}, {} }); // far

	auto v = g.visible_from({ 0, 0, 0 }, 1, NetId{ 1 });
	REQUIRE(v.size() == 1);
	CHECK(v[0] == NetId{ 2 });

	v = g.visible_from({ 0, 0, 0 }, 3, NetId{ 1 });
	REQUIRE(v.size() == 2);
	CHECK(v[0] == NetId{ 2 });
	CHECK(v[1] == NetId{ 3 });
}

TEST_CASE("diff_interest partitions entered / stayed / left") {
	const std::vector<NetId> prev{ NetId{ 1 }, NetId{ 2 }, NetId{ 3 } };
	const std::vector<NetId> cur{ NetId{ 2 }, NetId{ 3 }, NetId{ 4 } };
	const auto d = vb::replication::diff_interest(prev, cur);
	REQUIRE(d.entered.size() == 1);
	CHECK(d.entered[0] == NetId{ 4 });
	REQUIRE(d.left.size() == 1);
	CHECK(d.left[0] == NetId{ 1 });
	CHECK(d.stayed.size() == 2);
}

// --- two clients see each other over the session stack -------------------

namespace {

struct TwoClientWorld {
	LoopbackNetwork net;
	ServerSession server;
	std::optional<ClientSession> a;
	std::optional<ClientSession> b;
	NetId a_id{ 0 };
	NetId b_id{ 0 };
	ConnId b_conn{ 0 };

	TwoClientWorld() : server(net.server(), [] {
				HandshakeServerConfig c;
				c.world_seed = 1;
				return c;
			}()) {
		server.set_interest_radius_cells(1);
		REQUIRE(net.server().listen(0));

		Transport &ta = net.create_client();
		auto ida = ta.connect("x", 0);
		REQUIRE(ida);
		a.emplace(ta, *ida, HandshakeClientConfig{ "A", "", "v", 1 });

		Transport &tb = net.create_client();
		auto idb = tb.connect("x", 0);
		REQUIRE(idb);
		b_conn = *idb;
		b.emplace(tb, *idb, HandshakeClientConfig{ "B", "", "v", 2 });

		pump(16);
		REQUIRE(a->joined());
		REQUIRE(b->joined());
		a_id = a->join_accept()->your_net_id;
		b_id = b->join_accept()->your_net_id;
	}

	void pump(int rounds) {
		for (int i = 0; i < rounds; ++i) {
			server.tick(0.05);
			a->tick(0.05);
			b->tick(0.05);
		}
	}
};

} // namespace

TEST_CASE("two clients within interest range replicate to each other") {
	TwoClientWorld w;

	// Both near the origin (default 32 m cells, radius 1 -> same/adjacent cell).
	w.server.set_player_state(w.a_id, Vec3d{ 0, 64, 0 });
	w.server.set_player_state(w.b_id, Vec3d{ 8, 64, 0 });
	w.pump(4);

	REQUIRE(w.a->remote_entities().count(w.b_id) == 1);
	REQUIRE(w.b->remote_entities().count(w.a_id) == 1);
	CHECK(w.a->remote_entities().at(w.b_id).pos.x == doctest::Approx(8.0));

	// Move B far away -> A should get a removal.
	w.server.set_player_state(w.b_id, Vec3d{ 5000, 64, 0 });
	w.pump(4);
	CHECK(w.a->remote_entities().count(w.b_id) == 0);
	CHECK(w.b->remote_entities().count(w.a_id) == 0);

	// Bring B back -> re-enters.
	w.server.set_player_state(w.b_id, Vec3d{ 4, 64, 4 });
	w.pump(4);
	CHECK(w.a->remote_entities().count(w.b_id) == 1);
}

TEST_CASE("a leaving player is dropped from the other's view") {
	TwoClientWorld w;
	w.server.set_player_state(w.a_id, Vec3d{ 0, 64, 0 });
	w.server.set_player_state(w.b_id, Vec3d{ 4, 64, 0 });
	w.pump(4);
	REQUIRE(w.a->remote_entities().count(w.b_id) == 1);

	w.net.server().close(w.b_conn, "left");
	w.pump(3);
	CHECK(w.a->remote_entities().count(w.b_id) == 0);
	CHECK(w.server.player_count() == 1);
}
