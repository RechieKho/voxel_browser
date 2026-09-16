#include <doctest/doctest.h>

#include <ostream>

#include <optional>
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
			{ "base:air", false, false, false, 0 },
			{ "base:stone", true, true, false, 0 },
			{ "test:glow", true, true, false, 15 },
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
