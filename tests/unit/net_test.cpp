#include <doctest/doctest.h>

#include <ostream>

#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "vb/net/handshake.hpp"
#include "vb/net/loopback.hpp"
#include "vb/net/transport.hpp"
#include "vb/protocol/handshake.hpp"
#include "vb/protocol/message.hpp"

using namespace vb::net;
namespace proto = vb::protocol;

namespace {

std::span<const std::byte> span_of(const std::vector<std::byte> &v) {
	return { v.data(), v.size() };
}

// Drives a ServerHandshake per connection and one ClientHandshake over a
// LoopbackNetwork until both sides settle.
struct Harness {
	LoopbackNetwork net;
	Transport &server = net.server();
	Transport &client = net.create_client();

	HandshakeServerConfig server_cfg;
	HandshakeServerHost server_host;
	std::optional<ClientHandshake> client_hs;

	std::map<ConnId, ServerHandshake> server_hs;
	bool server_completed = false;
	std::string server_player;
	bool client_completed = false;
	bool client_failed = false;
	std::string client_failure;
	bool client_disconnected = false;

	void send_all(Transport &t, ConnId conn,
			const std::vector<OutgoingFrame> &frames) {
		for (const auto &f : frames) {
			t.send(conn, f.lane, span_of(f.bytes));
		}
	}

	void start() {
		REQUIRE(server.listen(0));
		auto id = client.connect("loopback", 0);
		REQUIRE(id);
		client_hs.emplace(HandshakeClientConfig{ "Tester", "", "vb-test", 1 });
	}

	void pump_server() {
		std::vector<TransportEvent> events;
		server.poll(events);
		for (auto &ev : events) {
			if (ev.kind == TransportEvent::Kind::kConnected) {
				server_hs.emplace(ev.conn,
						ServerHandshake(server_cfg, server_host));
			} else if (ev.kind == TransportEvent::Kind::kMessage) {
				std::size_t consumed = 0;
				auto frame = proto::read_frame(span_of(ev.frame), consumed);
				REQUIRE(frame);
				auto it = server_hs.find(ev.conn);
				REQUIRE(it != server_hs.end());
				auto step = it->second.on_frame(*frame);
				send_all(server, ev.conn, step.send);
				if (step.completed) {
					server_completed = true;
					server_player = step.player_name;
				}
				if (step.disconnect) {
					server.close(ev.conn, "handshake failed");
				}
			}
		}
	}

	void pump_client() {
		std::vector<TransportEvent> events;
		client.poll(events);
		for (auto &ev : events) {
			if (ev.kind == TransportEvent::Kind::kConnected) {
				send_all(client, ev.conn, client_hs->start().send);
			} else if (ev.kind == TransportEvent::Kind::kMessage) {
				std::size_t consumed = 0;
				auto frame = proto::read_frame(span_of(ev.frame), consumed);
				REQUIRE(frame);
				auto step = client_hs->on_frame(*frame);
				send_all(client, ev.conn, step.send);
				if (step.completed) {
					client_completed = true;
				}
				if (step.failed) {
					client_failed = true;
					client_failure = step.failure_reason;
				}
			} else if (ev.kind == TransportEvent::Kind::kDisconnected) {
				client_disconnected = true;
			}
		}
	}

	void run(int max_rounds = 12) {
		for (int i = 0; i < max_rounds; ++i) {
			pump_server();
			pump_client();
		}
	}
};

} // namespace

TEST_CASE("loopback transport delivers messages both ways and closes") {
	LoopbackNetwork net;
	Transport &server = net.server();
	Transport &client = net.create_client();
	REQUIRE(server.listen(0));

	auto id = client.connect("x", 0);
	REQUIRE(id);

	std::vector<TransportEvent> ev;
	server.poll(ev);
	REQUIRE(ev.size() == 1);
	CHECK(ev[0].kind == TransportEvent::Kind::kConnected);
	CHECK(server.connection_count() == 1);

	ev.clear();
	client.poll(ev);
	REQUIRE(ev.size() == 1);
	CHECK(ev[0].kind == TransportEvent::Kind::kConnected);

	std::vector<std::byte> payload;
	proto::ByteWriter(payload).string("ping");
	std::vector<std::byte> frame;
	proto::write_frame(frame, proto::MessageType::kC2SChat, payload);
	client.send(*id, proto::Lane::kControl, span_of(frame));

	ev.clear();
	server.poll(ev);
	REQUIRE(ev.size() == 1);
	REQUIRE(ev[0].kind == TransportEvent::Kind::kMessage);
	std::size_t consumed = 0;
	auto parsed = proto::read_frame(span_of(ev[0].frame), consumed);
	REQUIRE(parsed);
	CHECK(parsed->header.type == proto::MessageType::kC2SChat);

	server.close(*id, "bye");
	ev.clear();
	client.poll(ev);
	REQUIRE(ev.size() == 1);
	CHECK(ev[0].kind == TransportEvent::Kind::kDisconnected);
	CHECK(ev[0].reason == "bye");
}

TEST_CASE("connect fails when the server is not listening") {
	LoopbackNetwork net;
	Transport &client = net.create_client();
	auto id = client.connect("x", 0);
	CHECK_FALSE(id);
	CHECK(id.error() == vb::core::NetError::kConnectFailed);
}

TEST_CASE("full handshake, auth_mode=none") {
	Harness h;
	h.server_host.on_ready = [](std::string_view) {
		return JoinGrant{ vb::core::NetId{ 42 }, { 10.0, 70.0, -5.0 }, 0xC0FFEE, 600 };
	};
	h.start();
	h.run();

	CHECK(h.server_completed);
	CHECK(h.server_player == "Tester");
	CHECK(h.client_completed);
	CHECK_FALSE(h.client_failed);

	REQUIRE(h.client_hs->status() == ClientHandshakeStatus::kJoined);
	REQUIRE(h.client_hs->server_info().has_value());
	CHECK(h.client_hs->server_info()->auth_mode == proto::AuthMode::kNone);
	REQUIRE(h.client_hs->join_accept().has_value());
	CHECK(h.client_hs->join_accept()->your_net_id == vb::core::NetId{ 42 });
	CHECK(h.client_hs->join_accept()->world_seed == 0xC0FFEE);
	CHECK(h.client_hs->join_accept()->spawn_pos.y == doctest::Approx(70.0));
}

TEST_CASE("handshake rejects a bad player name") {
	Harness h;
	h.server_host.authenticate = [](std::string_view, std::string_view) {
		return AuthOutcome{ false, "name taken" };
	};
	h.start();
	h.run();

	CHECK_FALSE(h.server_completed);
	CHECK(h.client_failed);
	CHECK(h.client_failure == "name taken");
	CHECK(h.client_disconnected);
}

TEST_CASE("handshake rejects a full server") {
	Harness h;
	h.server_cfg.max_players = 2;
	h.server_host.current_player_count = [] { return 2u; };
	h.start();
	h.run();

	CHECK(h.client_failed);
	CHECK(h.client_hs->status() == ClientHandshakeStatus::kFailed);
}

TEST_CASE("server rejects an out-of-order message") {
	Harness h;
	h.start();

	// Let the connection establish, then send Ready instead of Hello.
	h.pump_server();
	h.pump_client(); // client sends Hello here...
	// override: manufacture a bogus first message by driving a fresh FSM
	ServerHandshake fsm(h.server_cfg, h.server_host);
	std::vector<std::byte> payload;
	proto::C2SReady{}.encode(payload);
	std::vector<std::byte> frame;
	proto::write_frame(frame, proto::MessageType::kC2SReady, payload);
	std::size_t consumed = 0;
	auto parsed = proto::read_frame(span_of(frame), consumed);
	REQUIRE(parsed);
	auto step = fsm.on_frame(*parsed);
	CHECK(step.disconnect);
	CHECK(step.disconnect_reason == proto::DisconnectReason::kBadHandshake);
	CHECK(fsm.state() == ServerHandshakeState::kClosed);
}

TEST_CASE("client rejects a protocol version mismatch") {
	ClientHandshake client(HandshakeClientConfig{ "P", "", "v", 0 });
	client.start();

	proto::S2CServerInfo info;
	info.engine_protocol_version = 0xFFFF;
	info.auth_mode = proto::AuthMode::kNone;
	std::vector<std::byte> payload;
	info.encode(payload);
	std::vector<std::byte> frame;
	proto::write_frame(frame, proto::MessageType::kS2CServerInfo, payload);
	std::size_t consumed = 0;
	auto parsed = proto::read_frame(span_of(frame), consumed);
	REQUIRE(parsed);

	auto step = client.on_frame(*parsed);
	CHECK(step.failed);
	CHECK(client.status() == ClientHandshakeStatus::kFailed);
}

TEST_CASE("server handshake times out") {
	ServerHandshake fsm(HandshakeServerConfig{}, HandshakeServerHost{});
	auto step = fsm.on_timeout();
	CHECK(step.disconnect);
	CHECK(step.disconnect_reason == proto::DisconnectReason::kTimeout);
}
