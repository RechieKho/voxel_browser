#include <doctest/doctest.h>

#include <ostream>

#include "vb/net/gns_transport.hpp"

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
	GnsTransport server;
	REQUIRE(server.listen(0)); // port 0 -> OS picks a free port
	REQUIRE(server.bound_port() != 0);
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

#endif // VB_WITH_NET
