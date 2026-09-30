#include <doctest/doctest.h>

#include <ostream>

#include <optional>
#include <string>
#include <vector>

#include "vb/net/loopback.hpp"
#include "vb/net/session.hpp"
#include "vb/world/block.hpp"

using namespace vb::net;

namespace {

void pump(ServerSession &server, ClientSession &client, int n) {
	for (int i = 0; i < n; ++i) {
		server.tick(0.05);
		client.tick(0.05);
	}
}

} // namespace

TEST_CASE("server sends a custom block registry and the client applies it") {
	LoopbackNetwork net;
	HandshakeServerConfig cfg;
	HandshakeServerHost host;
	host.block_registry =
			[]() -> std::optional<std::vector<vb::protocol::BlockRegistryRecord>> {
		return std::vector<vb::protocol::BlockRegistryRecord>{
			{ .name = "base:air", .solid = false, .opaque = false, .liquid = false, .light_emission = 0, .texture = "", .crack_texture = "" },
			{ .name = "base:stone", .solid = true, .opaque = true, .liquid = false, .light_emission = 0, .texture = "", .crack_texture = "" },
			{ .name = "test:glow", .solid = true, .opaque = true, .liquid = false, .light_emission = 15, .texture = "", .crack_texture = "" },
		};
	};
	ServerSession server(net.server(), cfg, host);
	REQUIRE(net.server().listen(0));

	Transport &t = net.create_client();
	auto id = t.connect("x", 0);
	REQUIRE(id);
	ClientSession client(t, *id, HandshakeClientConfig{ "P", "", "v", 1 });

	pump(server, client, 10);
	REQUIRE(client.joined());

	const auto &reg = client.chunk_store().registry();
	CHECK(reg.size() == 3);
	const auto glow_id = reg.find("test:glow");
	CHECK(glow_id != vb::core::BlockId::kAir); // present, and not the air slot
	CHECK(reg.light_emission(glow_id) == 15);
	CHECK(reg.is_solid(glow_id));
}

TEST_CASE("server sends max_damage and crack_texture and the client applies "
		  "both (regression: these two fields used to be silently dropped by "
		  "src/server/main.cpp's/src/client/main.cpp's own aggregate-init)") {
	LoopbackNetwork net;
	HandshakeServerConfig cfg;
	HandshakeServerHost host;
	host.block_registry =
			[]() -> std::optional<std::vector<vb::protocol::BlockRegistryRecord>> {
		vb::protocol::BlockRegistryRecord ore;
		ore.name = "test:ore";
		ore.solid = true;
		ore.opaque = true;
		ore.max_damage = 6;
		ore.crack_texture = "textures/ore_crack.png";
		return std::vector<vb::protocol::BlockRegistryRecord>{
			{ .name = "base:air", .solid = false, .opaque = false, .liquid = false, .light_emission = 0, .texture = "", .crack_texture = "" },
			ore
		};
	};
	ServerSession server(net.server(), cfg, host);
	REQUIRE(net.server().listen(0));

	Transport &t = net.create_client();
	auto id = t.connect("x", 0);
	REQUIRE(id);
	ClientSession client(t, *id, HandshakeClientConfig{ "P", "", "v", 1 });

	pump(server, client, 10);
	REQUIRE(client.joined());

	const auto &reg = client.chunk_store().registry();
	const auto ore_id = reg.find("test:ore");
	REQUIRE(ore_id != vb::core::BlockId::kAir);
	CHECK(reg.get(ore_id).max_damage == 6);
	CHECK(reg.get(ore_id).crack_texture == "textures/ore_crack.png");
}

TEST_CASE("without a block_registry hook, the client keeps its own base() "
		  "registry") {
	LoopbackNetwork net;
	HandshakeServerConfig cfg;
	ServerSession server(net.server(), cfg); // default host: no hook set
	REQUIRE(net.server().listen(0));

	Transport &t = net.create_client();
	auto id = t.connect("x", 0);
	REQUIRE(id);
	ClientSession client(t, *id, HandshakeClientConfig{ "P", "", "v", 1 });

	pump(server, client, 10);
	REQUIRE(client.joined());

	CHECK(client.chunk_store().registry().size() ==
			vb::world::BlockRegistry::base().size());
}

TEST_CASE("server sends a custom keybind registry and the client applies it") {
	LoopbackNetwork net;
	HandshakeServerConfig cfg;
	HandshakeServerHost host;
	host.keybind_registry = []() -> std::optional<std::vector<std::string>> {
		return std::vector<std::string>{ "dash", "interact", "toggle_map" };
	};
	ServerSession server(net.server(), cfg, host);
	REQUIRE(net.server().listen(0));

	Transport &t = net.create_client();
	auto id = t.connect("x", 0);
	REQUIRE(id);
	ClientSession client(t, *id, HandshakeClientConfig{ "P", "", "v", 1 });

	pump(server, client, 10);
	REQUIRE(client.joined());

	const auto &names = client.registered_keybinds();
	REQUIRE(names.size() == 3);
	CHECK(names[0] == "dash");
	CHECK(names[2] == "toggle_map");
}

TEST_CASE("without a keybind_registry hook, the client has no registered "
		  "keybinds") {
	LoopbackNetwork net;
	HandshakeServerConfig cfg;
	ServerSession server(net.server(), cfg); // default host: no hook set
	REQUIRE(net.server().listen(0));

	Transport &t = net.create_client();
	auto id = t.connect("x", 0);
	REQUIRE(id);
	ClientSession client(t, *id, HandshakeClientConfig{ "P", "", "v", 1 });

	pump(server, client, 10);
	REQUIRE(client.joined());

	CHECK(client.registered_keybinds().empty());
}

TEST_CASE("server sends a custom entity kind registry and the client applies "
		  "it") {
	LoopbackNetwork net;
	HandshakeServerConfig cfg;
	HandshakeServerHost host;
	host.entity_kind_registry =
			[]() -> std::optional<std::vector<vb::protocol::EntityKindRegistryRecord>> {
		return std::vector<vb::protocol::EntityKindRegistryRecord>{
			{ .name = "test:slime", .width = 0.6f, .height = 0.6f, .visual = std::nullopt },
			{ .name = "test:golem", .width = 1.2f, .height = 2.4f, .visual = std::nullopt },
		};
	};
	ServerSession server(net.server(), cfg, host);
	REQUIRE(net.server().listen(0));

	Transport &t = net.create_client();
	auto id = t.connect("x", 0);
	REQUIRE(id);
	ClientSession client(t, *id, HandshakeClientConfig{ "P", "", "v", 1 });

	pump(server, client, 10);
	REQUIRE(client.joined());

	const auto &kinds = client.entity_kind_registry();
	REQUIRE(kinds.size() == 2);
	CHECK(kinds[0].name == "test:slime");
	CHECK(kinds[0].width == doctest::Approx(0.6f));
	CHECK(kinds[1].name == "test:golem");
	CHECK(kinds[1].height == doctest::Approx(2.4f));

	// EntityKindId 1 -> kinds[0], EntityKindId::kInvalid (players) -> nullptr.
	const auto *slime = client.entity_kind(static_cast<vb::core::EntityKindId>(1));
	REQUIRE(slime != nullptr);
	CHECK(slime->name == "test:slime");
	CHECK(client.entity_kind(vb::core::EntityKindId::kInvalid) == nullptr);
	CHECK(client.entity_kind(static_cast<vb::core::EntityKindId>(99)) == nullptr);
}

TEST_CASE("without an entity_kind_registry hook, the client has no "
		  "registered entity kinds") {
	LoopbackNetwork net;
	HandshakeServerConfig cfg;
	ServerSession server(net.server(), cfg); // default host: no hook set
	REQUIRE(net.server().listen(0));

	Transport &t = net.create_client();
	auto id = t.connect("x", 0);
	REQUIRE(id);
	ClientSession client(t, *id, HandshakeClientConfig{ "P", "", "v", 1 });

	pump(server, client, 10);
	REQUIRE(client.joined());

	CHECK(client.entity_kind_registry().empty());
}

TEST_CASE("server sends custom move params and the client's prediction uses "
		  "them (Phase 6.7)") {
	LoopbackNetwork net;
	HandshakeServerConfig cfg;
	HandshakeServerHost host;
	host.move_params = []() -> std::optional<vb::protocol::S2CMoveParams> {
		vb::protocol::S2CMoveParams p;
		p.gravity = 3.5; // e.g. a low-gravity pack
		p.jump_speed = 4.0;
		return p;
	};
	ServerSession server(net.server(), cfg, host);
	REQUIRE(net.server().listen(0));

	Transport &t = net.create_client();
	auto id = t.connect("x", 0);
	REQUIRE(id);
	ClientSession client(t, *id, HandshakeClientConfig{ "P", "", "v", 1 });

	pump(server, client, 10);
	REQUIRE(client.joined());

	CHECK(client.move_params().gravity == doctest::Approx(3.5));
	CHECK(client.move_params().jump_speed == doctest::Approx(4.0));
}

TEST_CASE("without a move_params hook, the client keeps its own default "
		  "MoveParams") {
	LoopbackNetwork net;
	HandshakeServerConfig cfg;
	ServerSession server(net.server(), cfg); // default host: no hook set
	REQUIRE(net.server().listen(0));

	Transport &t = net.create_client();
	auto id = t.connect("x", 0);
	REQUIRE(id);
	ClientSession client(t, *id, HandshakeClientConfig{ "P", "", "v", 1 });

	pump(server, client, 10);
	REQUIRE(client.joined());

	CHECK(client.move_params().gravity ==
			doctest::Approx(vb::physics::MoveParams{}.gravity));
}
