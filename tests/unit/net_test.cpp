#include <doctest/doctest.h>

#include <ostream>

#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "vb/net/handshake.hpp"
#include "vb/net/integrated.hpp"
#include "vb/net/loopback.hpp"
#include "vb/net/session.hpp"
#include "vb/net/transport.hpp"
#include "vb/protocol/chat.hpp"
#include "vb/protocol/compression.hpp"
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
	std::uint8_t client_flags = 0; // set before start()

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
		client_hs.emplace(HandshakeClientConfig{ "Tester", "", "vb-test", 1, client_flags });
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

TEST_CASE("server refuses an automation client unless it accepts them") {
	Harness h;
	h.server_cfg.accept_automation_clients = false;
	h.client_flags = proto::kClientFlagAutomation;
	h.start();
	h.run();

	CHECK_FALSE(h.server_completed);
	CHECK(h.client_failed);
	CHECK(h.client_failure == "automation clients are not accepted by this server");
	CHECK(h.client_disconnected);
}

TEST_CASE("server accepts an automation client when configured to") {
	Harness h;
	h.server_cfg.accept_automation_clients = true;
	h.client_flags = proto::kClientFlagAutomation;
	h.start();
	h.run();

	CHECK(h.server_completed);
	CHECK_FALSE(h.client_failed);
}

// Only meaningful without VB_WITH_AUTOMATION: an automation build's
// ClientHandshake always adds kClientFlagAutomation itself, so a client
// without that bit doesn't exist there (the refusal case above covers that build).
#if !defined(VB_WITH_AUTOMATION)
TEST_CASE("server ignores unknown client flag bits and never needs them") {
	Harness h;
	h.server_cfg.accept_automation_clients = false;
	h.client_flags = 0x80; // not kClientFlagAutomation
	h.start();
	h.run();

	CHECK(h.server_completed);
	CHECK_FALSE(h.client_failed);
}

TEST_CASE("a plain client is accepted by a server that refuses automation") {
	Harness h;
	h.server_cfg.accept_automation_clients = false;
	h.start();
	h.run();

	CHECK(h.server_completed);
	CHECK_FALSE(h.client_failed);
}
#endif

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

TEST_CASE("IntegratedGame completes the join and assigns a net id") {
	HandshakeServerConfig scfg;
	scfg.world_seed = 12345;
	HandshakeClientConfig ccfg;
	ccfg.player_name = "Solo";

	IntegratedGame game(scfg, ccfg);
	for (int i = 0; i < 32 && !game.client_joined() && !game.client_failed();
			++i) {
		game.tick(0.05);
	}

	REQUIRE(game.client_joined());
	CHECK(game.server().player_count() == 1);
	REQUIRE(game.client().join_accept().has_value());
	CHECK(game.client().join_accept()->your_net_id == vb::core::NetId{ 1 });
	CHECK(game.client().join_accept()->world_seed == 12345);

	auto joins = game.server().take_joins();
	REQUIRE(joins.size() == 1);
	CHECK(joins[0].name == "Solo");
	CHECK(joins[0].net_id == vb::core::NetId{ 1 });
}

TEST_CASE("ServerSession times out a silent connection") {
	LoopbackNetwork net;
	HandshakeServerConfig cfg;
	cfg.handshake_timeout_seconds = 1.0;
	ServerSession server(net.server(), cfg);
	REQUIRE(net.server().listen(0));

	Transport &ct = net.create_client();
	auto id = ct.connect("x", 0);
	REQUIRE(id);

	server.tick(0.5); // sees Connected
	CHECK(server.pending_count() == 1);
	server.tick(1.0); // past the timeout

	std::vector<TransportEvent> ev;
	ct.poll(ev);
	bool disconnected = false;
	for (const auto &e : ev) {
		disconnected |= e.kind == TransportEvent::Kind::kDisconnected;
	}
	CHECK(disconnected);
}

#if VB_WITH_COMPRESSION

// ARCHITECTURE_SPEC.md §18 Q4: frame_message() is the one real caller of
// compress_lz4() (protocol_test.cpp covers that function directly) -- these
// cover its own size-threshold + "only if it actually helps" policy.
TEST_CASE("frame_message compresses a large, repetitive payload and sets "
		"MessageFlag::kCompressed") {
	proto::S2CChat msg;
	msg.text = std::string(500, 'a'); // well past kCompressionThresholdBytes, highly repetitive
	std::vector<std::byte> uncompressed_payload;
	msg.encode(uncompressed_payload);
	REQUIRE(uncompressed_payload.size() >= kCompressionThresholdBytes);

	const auto framed = frame_message(msg);
	std::size_t consumed = 0;
	auto frame = proto::read_frame(span_of(framed.bytes), consumed);
	REQUIRE(frame);
	CHECK((frame->header.flags &
				  static_cast<std::uint16_t>(proto::MessageFlag::kCompressed)) != 0);
	// Genuinely smaller on the wire than sending it uncompressed would be --
	// not just flagged.
	CHECK(framed.bytes.size() < proto::kEnvelopeBytes + uncompressed_payload.size());

	auto decoded = proto::decompress_lz4(frame->payload);
	REQUIRE(decoded);
	auto reconstructed = proto::S2CChat::decode(*decoded);
	REQUIRE(reconstructed);
	CHECK(reconstructed->text == msg.text);
}

TEST_CASE("frame_message leaves a small payload uncompressed") {
	proto::S2CChat msg;
	msg.text = "hi"; // well under kCompressionThresholdBytes

	const auto framed = frame_message(msg);
	std::size_t consumed = 0;
	auto frame = proto::read_frame(span_of(framed.bytes), consumed);
	REQUIRE(frame);
	CHECK((frame->header.flags &
				  static_cast<std::uint16_t>(proto::MessageFlag::kCompressed)) == 0);
	auto decoded = proto::S2CChat::decode(frame->payload);
	REQUIRE(decoded);
	CHECK(decoded->text == "hi");
}

#endif // VB_WITH_COMPRESSION
