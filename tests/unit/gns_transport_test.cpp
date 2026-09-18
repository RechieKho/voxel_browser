#include <doctest/doctest.h>

#include <ostream>

#include "vb/net/gns_transport.hpp"
#include "vb/net/handshake.hpp"
#include "vb/net/session.hpp"

#if !VB_WITH_NET

TEST_CASE("GnsTransport reports kBackendUnavailable without VB_WITH_NET") {
	vb::net::GnsTransport t;
	const auto status = t.listen(0);
	CHECK_FALSE(status);
	CHECK(status.error() == vb::core::NetError::kBackendUnavailable);
	const auto conn = t.connect("127.0.0.1", 27015);
	CHECK_FALSE(conn);
}

#else

#include <chrono>
#include <thread>
#include <vector>

#include "vb/protocol/message.hpp"

using namespace vb::net;

namespace {

// Real UDP: pump until `pred()` is true or the attempt budget runs out.
template <typename Pred>
bool pump_until(int attempts, Pred pred) {
	for (int i = 0; i < attempts; ++i) {
		if (pred()) {
			return true;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
	}
	return pred();
}

} // namespace

TEST_CASE("GnsTransport: connect, exchange a message, and disconnect over real UDP") {
	// GNS's direct-UDP listen path rejects port 0 ("Must specify local
	// port."), unlike a plain BSD socket -- it has no ephemeral-port
	// allocation, so tests must pick a concrete port themselves.
	constexpr std::uint16_t kTestPort = 27201;

	GnsTransport server;
	REQUIRE(server.listen(kTestPort));
	REQUIRE(server.bound_port() == kTestPort);
	CHECK(server.is_server());

	GnsTransport client;
	const auto conn = client.connect("127.0.0.1", server.bound_port());
	REQUIRE(conn);
	CHECK_FALSE(client.is_server());

	bool client_connected = false;
	bool server_connected = false;
	ConnId server_side_conn = ConnId::kInvalid;

	const bool both_connected = pump_until(500, [&] {
		std::vector<TransportEvent> ev;
		client.poll(ev);
		for (const auto &e : ev) {
			if (e.kind == TransportEvent::Kind::kConnected) {
				client_connected = true;
			}
		}
		ev.clear();
		server.poll(ev);
		for (const auto &e : ev) {
			if (e.kind == TransportEvent::Kind::kConnected) {
				server_connected = true;
				server_side_conn = e.conn;
			}
		}
		return client_connected && server_connected;
	});
	REQUIRE(both_connected);
	CHECK(server.connection_count() == 1);
	CHECK(client.connection_count() == 1);

	const std::vector<std::byte> payload{ std::byte{ 1 }, std::byte{ 2 },
		std::byte{ 3 }, std::byte{ 4 } };
	client.send(*conn, vb::protocol::Lane::kControl, payload);

	std::vector<std::byte> received;
	const bool got_message = pump_until(400, [&] {
		std::vector<TransportEvent> ev;
		server.poll(ev);
		for (const auto &e : ev) {
			if (e.kind == TransportEvent::Kind::kMessage && e.conn == server_side_conn) {
				received = e.frame;
			}
		}
		return !received.empty();
	});
	REQUIRE(got_message);
	CHECK(received == payload);

	// Client-initiated close: the client sees it immediately (synthesized),
	// the server learns about it asynchronously via its own poll().
	client.close(*conn, "bye");
	CHECK(client.connection_count() == 0);

	bool server_saw_disconnect = false;
	const bool disconnected = pump_until(400, [&] {
		std::vector<TransportEvent> ev;
		server.poll(ev);
		for (const auto &e : ev) {
			if (e.kind == TransportEvent::Kind::kDisconnected &&
					e.conn == server_side_conn) {
				server_saw_disconnect = true;
			}
		}
		return server_saw_disconnect;
	});
	CHECK(disconnected);
	CHECK(server.connection_count() == 0);
}

// §8.3 hardening (REMAINING_TASKS.md 1.3): remote_address() is what
// ServerSession::set_max_connections_per_ip is built on -- confirmed here at
// the transport level, then end-to-end through a real ServerSession/
// ClientSession pair below.
TEST_CASE("GnsTransport::remote_address returns the real peer IP over UDP") {
	constexpr std::uint16_t kTestPort = 27202;

	GnsTransport server;
	REQUIRE(server.listen(kTestPort));
	GnsTransport client;
	const auto conn = client.connect("127.0.0.1", kTestPort);
	REQUIRE(conn);

	ConnId server_side_conn = ConnId::kInvalid;
	const bool connected = pump_until(500, [&] {
		std::vector<TransportEvent> cev;
		client.poll(cev);
		std::vector<TransportEvent> sev;
		server.poll(sev);
		for (const auto &e : sev) {
			if (e.kind == TransportEvent::Kind::kConnected) {
				server_side_conn = e.conn;
			}
		}
		return server_side_conn != ConnId::kInvalid;
	});
	REQUIRE(connected);

	const auto addr = server.remote_address(server_side_conn);
	REQUIRE(addr.has_value());
	CHECK(*addr == "127.0.0.1");

	// An unknown/never-connected id: nullopt, not a crash.
	CHECK_FALSE(server.remote_address(static_cast<ConnId>(0xDEADBEEF)).has_value());
}

TEST_CASE("ServerSession rejects a connection beyond the per-IP cap over real UDP") {
	constexpr std::uint16_t kTestPort = 27203;

	GnsTransport server_transport;
	REQUIRE(server_transport.listen(kTestPort));
	HandshakeServerConfig cfg;
	cfg.world_seed = 7;
	ServerSession server(server_transport, cfg);
	server.set_max_connections_per_ip(1);

	GnsTransport client1_transport;
	const auto conn1 = client1_transport.connect("127.0.0.1", kTestPort);
	REQUIRE(conn1);
	ClientSession client1(client1_transport, *conn1,
			HandshakeClientConfig{ "A", "", "v", 1 });

	const bool first_joined = pump_until(500, [&] {
		server.tick(0.05);
		client1.tick(0.05);
		return client1.joined();
	});
	REQUIRE(first_joined);

	// A second connection from the same address (127.0.0.1): the cap is
	// already at 1, so the server-side Transport::close()s it before any
	// handshake traffic even starts -- the client sees a failed connection,
	// not a rejected auth (this is a transport-level policy, not
	// HandshakeServerConfig::max_players' application-level one).
	GnsTransport client2_transport;
	const auto conn2 = client2_transport.connect("127.0.0.1", kTestPort);
	REQUIRE(conn2);
	ClientSession client2(client2_transport, *conn2,
			HandshakeClientConfig{ "B", "", "v", 1 });

	pump_until(500, [&] {
		server.tick(0.05);
		client1.tick(0.05);
		client2.tick(0.05);
		return client2.joined() || client2.failed();
	});

	CHECK_FALSE(client2.joined());
	CHECK(client2.failed());
	CHECK(client1.joined()); // the first connection is unaffected
}

// REMAINING_TASKS.md 1.3 polish: SteamNetworkingIPAddr::ParseString() only
// ever accepts numeric IP literals -- "localhost" used to fail connect()
// outright before GnsTransport::connect() gained a getaddrinfo() fallback.
TEST_CASE("GnsTransport::connect resolves a real hostname (\"localhost\"), "
		"not just numeric IP literals") {
	constexpr std::uint16_t kTestPort = 27204;

	GnsTransport server;
	REQUIRE(server.listen(kTestPort));

	GnsTransport client;
	const auto conn = client.connect("localhost", kTestPort);
	REQUIRE(conn);

	bool server_connected = false;
	const bool connected = pump_until(500, [&] {
		std::vector<TransportEvent> cev;
		client.poll(cev);
		std::vector<TransportEvent> sev;
		server.poll(sev);
		for (const auto &e : sev) {
			if (e.kind == TransportEvent::Kind::kConnected) {
				server_connected = true;
			}
		}
		return server_connected;
	});
	CHECK(connected);

	// A genuinely unresolvable name still fails cleanly, same as before.
	const auto bad = client.connect(
			"this-hostname-should-never-resolve.invalid", kTestPort);
	CHECK_FALSE(bad);
}

#endif // VB_WITH_NET
