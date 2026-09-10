#include <doctest/doctest.h>

#include <ostream>

#include <optional>

#include "vb/net/integrated.hpp"
#include "vb/net/loopback.hpp"
#include "vb/net/session.hpp"
#include "vb/protocol/input.hpp"

using namespace vb::net;
using vb::core::NetId;
using vb::core::Vec3d;
using vb::protocol::InputCmd;

namespace {

InputCmd forward_cmd(std::uint32_t seq) {
	InputCmd c;
	c.seq = seq;
	c.dt = 0.05f;
	c.move.z = 1.0f; // forward
	c.yaw = 90.0f; // -> +X world
	return c;
}

} // namespace

TEST_CASE("C2SInputBatch round-trips") {
	vb::protocol::C2SInputBatch in;
	in.cmds = { forward_cmd(1), forward_cmd(2), forward_cmd(3) };
	in.cmds[2].buttons = vb::protocol::kInputJump | vb::protocol::kInputSprint;

	std::vector<std::byte> bytes;
	in.encode(bytes);
	auto out = vb::protocol::C2SInputBatch::decode(
			{ bytes.data(), bytes.size() });
	REQUIRE(out);
	REQUIRE(out->cmds.size() == 3);
	CHECK(out->cmds[2].seq == 3);
	CHECK(out->cmds[2].buttons ==
			(vb::protocol::kInputJump | vb::protocol::kInputSprint));
	CHECK(out->cmds[0].move.z == doctest::Approx(1.0));
}

TEST_CASE("integrated: client prediction converges to server authority") {
	HandshakeServerConfig sc;
	sc.world_seed = 1;
	IntegratedGame game(sc, HandshakeClientConfig{ "P", "", "v", 1 });

	for (int i = 0; i < 32 && !game.client_joined(); ++i) {
		game.tick(0.05);
	}
	REQUIRE(game.client_joined());

	const NetId id = game.client().join_accept()->your_net_id;
	const Vec3d spawn = game.client().join_accept()->spawn_pos;
	game.client().set_local_feet(spawn);

	// Fly so the test doesn't depend on terrain being streamed in.
	vb::physics::MoveParams fly;
	fly.fly = true;
	game.server().set_move_params(fly);
	game.client().set_move_params(fly);

	for (std::uint32_t seq = 1; seq <= 40; ++seq) {
		game.client().push_input(forward_cmd(seq));
		game.tick(0.05);
	}
	// Let the last acks arrive.
	for (int i = 0; i < 8; ++i) {
		game.tick(0.05);
	}

	const auto *srv = game.server().player_move_state(id);
	REQUIRE(srv != nullptr);
	const Vec3d predicted = game.client().predicted_feet();

	// Moved a meaningful distance along +X from spawn.
	CHECK(srv->position.x - spawn.x > 3.0);
	// Prediction agrees with the server within a small epsilon.
	CHECK(predicted.x == doctest::Approx(srv->position.x).epsilon(0.02));
	CHECK(predicted.z == doctest::Approx(srv->position.z).epsilon(0.02));
	CHECK(game.client().last_acked_input_seq() > 0);
	CHECK(game.client().unacked_input_count() < 40);
}

TEST_CASE("integrated: a second client sees the first move (interpolated)") {
	LoopbackNetwork net;
	ServerSession server(net.server(), [] {
		HandshakeServerConfig c;
		c.world_seed = 1;
		return c;
	}());
	vb::physics::MoveParams fly;
	fly.fly = true;
	server.set_move_params(fly);
	REQUIRE(net.server().listen(0));

	vb::net::Transport &ta = net.create_client();
	auto ida = ta.connect("x", 0);
	REQUIRE(ida);
	std::optional<ClientSession> a;
	a.emplace(ta, *ida, HandshakeClientConfig{ "A", "", "v", 1 });

	vb::net::Transport &tb = net.create_client();
	auto idb = tb.connect("x", 0);
	REQUIRE(idb);
	std::optional<ClientSession> b;
	b.emplace(tb, *idb, HandshakeClientConfig{ "B", "", "v", 2 });

	auto pump = [&](int n) {
		for (int i = 0; i < n; ++i) {
			server.tick(0.05);
			a->tick(0.05);
			b->tick(0.05);
		}
	};
	pump(16);
	REQUIRE(a->joined());
	REQUIRE(b->joined());
	const NetId a_id = a->join_accept()->your_net_id;
	a->set_move_params(fly);
	a->set_local_feet(a->join_accept()->spawn_pos);
	b->set_local_feet(b->join_accept()->spawn_pos);

	const Vec3d a_spawn = a->join_accept()->spawn_pos;
	Vec3d first;
	for (std::uint32_t seq = 1; seq <= 30; ++seq) {
		a->push_input(forward_cmd(seq));
		pump(1);
		if (seq == 5) {
			first = b->interpolated_pos(a_id);
		}
	}
	pump(4);

	REQUIRE(b->remote_entities().count(a_id) == 1);
	const Vec3d seen = b->interpolated_pos(a_id);
	CHECK(seen.x - a_spawn.x > 2.0);
	CHECK(seen.x > first.x); // it kept moving as B watched
}
