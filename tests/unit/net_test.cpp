#include <doctest/doctest.h>

#include <ostream>

#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "vb/core/version.hpp"
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
		return AuthOutcome{ false, "name taken", {} };
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

TEST_CASE("server accepts Ready right after the last asset chunk, even when that "
		"chunk used up the tick's pump_assets budget") {
	// Regression: pump_assets() only left kStreamingAssets at the start of its
	// *next* call, so a client that replied Ready as soon as it had the final
	// chunk could hit a server still "streaming" and be dropped with
	// "unexpected message while streaming assets" (seen intermittently in e2e
	// once the pack's chunk count landed on the per-tick budget boundary).
	const vb::core::AssetHash hash{ 1, 2 };
	auto manifest = std::make_shared<vb::assetsync::Manifest>();
	manifest->entries.push_back({ "ui/a.lua", hash, 4, vb::assetsync::AssetKind::kUi });
	manifest->total_bytes = 4;

	HandshakeServerHost host;
	host.asset_manifest = [manifest] { return manifest; };
	host.asset_file_bytes = [](vb::core::AssetHash) {
		return std::optional<std::vector<std::byte>>(std::vector<std::byte>(4));
	};
	ServerHandshake fsm(HandshakeServerConfig{}, host);

	auto feed = [&](auto msg) {
		std::vector<std::byte> payload;
		msg.encode(payload);
		std::vector<std::byte> frame;
		proto::write_frame(frame, decltype(msg)::kType, payload);
		std::size_t consumed = 0;
		auto parsed = proto::read_frame(span_of(frame), consumed);
		REQUIRE(parsed);
		return fsm.on_frame(*parsed);
	};

	REQUIRE_FALSE(feed(proto::C2SHello{ vb::kEngineProtocolVersion, 1, "t" }).disconnect);
	REQUIRE_FALSE(feed(proto::C2SAuth{ "P", "" }).disconnect);
	REQUIRE_FALSE(feed(proto::C2SAssetManifestRequest{}).disconnect);
	REQUIRE_FALSE(feed(proto::C2SAssetRequest{ { hash } }).disconnect);
	REQUIRE(fsm.state() == ServerHandshakeState::kStreamingAssets);

	// Budget of exactly one chunk == the whole (single-chunk) asset.
	const auto step = fsm.pump_assets(1);
	CHECK(step.send.size() == 1);
	CHECK(fsm.state() == ServerHandshakeState::kAwaitingReady);

	CHECK_FALSE(feed(proto::C2SReady{}).disconnect);
	CHECK(fsm.state() == ServerHandshakeState::kPlaying);
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

// ---------------------------------------------------------------------------
// External authentication (auth.md §5.2): challenge, async verification
// ---------------------------------------------------------------------------

namespace {

struct ExternalAuthFsm {
	HandshakeServerConfig config;
	HandshakeServerHost host;
	std::unique_ptr<ServerHandshake> fsm;
	int begin_calls = 0;
	std::string seen_token;
	std::string seen_nonce;
	std::optional<vb::net::AuthOutcome> verdict; // what the pending ticket returns

	ExternalAuthFsm() {
		config.auth_mode = proto::AuthMode::kExternal;
		host.auth_challenge = [] {
			proto::S2CAuthChallenge c;
			c.provider = "oidc";
			c.issuer = "https://idp.example";
			c.client_id = "vb";
			c.nonce = "nonce-1";
			return std::optional<proto::S2CAuthChallenge>(c);
		};
		host.begin_authenticate = [this](std::string_view token, std::string_view nonce) {
			++begin_calls;
			seen_token = std::string(token);
			seen_nonce = std::string(nonce);
			return vb::net::AuthTicket([this] { return verdict; });
		};
	}
	void build() { fsm = std::make_unique<ServerHandshake>(config, host); }

	template <typename Msg>
	vb::net::ServerHandshakeStep feed(Msg msg) {
		std::vector<std::byte> payload;
		msg.encode(payload);
		std::vector<std::byte> frame;
		proto::write_frame(frame, Msg::kType, payload);
		std::size_t consumed = 0;
		auto parsed = proto::read_frame(span_of(frame), consumed);
		REQUIRE(parsed);
		return fsm->on_frame(*parsed);
	}
};

proto::MessageType first_type(const vb::net::OutgoingFrame &f) {
	std::size_t consumed = 0;
	auto parsed = proto::read_frame(span_of(f.bytes), consumed);
	REQUIRE(parsed);
	return parsed->header.type;
}

} // namespace

TEST_CASE("external auth: Hello is answered with ServerInfo then AuthChallenge") {
	ExternalAuthFsm t;
	t.build();
	auto step = t.feed(proto::C2SHello{ vb::kEngineProtocolVersion, 1, "t" });
	REQUIRE(step.send.size() == 2);
	CHECK(first_type(step.send[0]) == proto::MessageType::kS2CServerInfo);
	CHECK(first_type(step.send[1]) == proto::MessageType::kS2CAuthChallenge);
	CHECK(t.fsm->state() == ServerHandshakeState::kAwaitingAuth);
	CHECK(t.fsm->timeout_seconds() == doctest::Approx(300.0));
}

TEST_CASE("external auth fails closed without a challenge or a verifier") {
	{
		ExternalAuthFsm t;
		t.host.auth_challenge = [] { return std::optional<proto::S2CAuthChallenge>{}; };
		t.build();
		auto step = t.feed(proto::C2SHello{ vb::kEngineProtocolVersion, 1, "t" });
		CHECK(step.disconnect);
		CHECK(step.disconnect_reason == proto::DisconnectReason::kAuthFailed);
	}
	{
		ExternalAuthFsm t;
		t.host.begin_authenticate = nullptr;
		t.build();
		t.feed(proto::C2SHello{ vb::kEngineProtocolVersion, 1, "t" });
		auto step = t.feed(proto::C2SAuth{ "ignored", "jwt" });
		CHECK(step.disconnect);
		CHECK(t.fsm->state() == ServerHandshakeState::kClosed);
	}
}

TEST_CASE("external auth: pending ticket resolves on poll_auth, accepted name wins") {
	ExternalAuthFsm t;
	t.build();
	t.feed(proto::C2SHello{ vb::kEngineProtocolVersion, 1, "t" });
	auto step = t.feed(proto::C2SAuth{ "ignored", "jwt-abc" });
	CHECK(step.send.empty());
	CHECK(t.fsm->state() == ServerHandshakeState::kVerifyingAuth);
	CHECK(t.seen_token == "jwt-abc");
	CHECK(t.seen_nonce == "nonce-1");

	// Still pending: nothing happens, extra client messages are refused.
	CHECK(t.fsm->poll_auth().send.empty());
	CHECK(t.fsm->state() == ServerHandshakeState::kVerifyingAuth);

	t.verdict = vb::net::AuthOutcome{ true, {}, "alice" };
	auto done = t.fsm->poll_auth();
	REQUIRE(done.send.size() == 1);
	CHECK(first_type(done.send[0]) == proto::MessageType::kS2CAuthResult);
	CHECK_FALSE(done.disconnect);
	CHECK(t.fsm->state() == ServerHandshakeState::kAwaitingAssetManifestRequest);
	CHECK(t.fsm->player_name() == "alice");
	CHECK(t.fsm->timeout_seconds() == doctest::Approx(t.config.handshake_timeout_seconds));
}

TEST_CASE("external auth: fast-path ticket resolves inside on_frame") {
	ExternalAuthFsm t;
	t.verdict = vb::net::AuthOutcome{ true, {}, "bob" };
	t.build();
	t.feed(proto::C2SHello{ vb::kEngineProtocolVersion, 1, "t" });
	auto step = t.feed(proto::C2SAuth{ "x", "jwt" });
	CHECK(step.send.size() == 1);
	CHECK(t.fsm->state() == ServerHandshakeState::kAwaitingAssetManifestRequest);
	CHECK(t.fsm->player_name() == "bob");
}

TEST_CASE("external auth: rejected token disconnects and never reaches the asset manifest") {
	ExternalAuthFsm t;
	t.verdict = vb::net::AuthOutcome{ false, "not accepted by this server", {} };
	t.build();
	t.feed(proto::C2SHello{ vb::kEngineProtocolVersion, 1, "t" });
	auto step = t.feed(proto::C2SAuth{ "x", "bad" });
	CHECK(step.disconnect);
	CHECK(step.disconnect_reason == proto::DisconnectReason::kAuthFailed);
	CHECK(t.fsm->state() == ServerHandshakeState::kClosed);

	// A manifest request after the rejection is refused (no manifest sent).
	auto after = t.feed(proto::C2SAssetManifestRequest{});
	for (const auto &f : after.send) {
		CHECK(first_type(f) != proto::MessageType::kS2CAssetManifest);
	}
}

TEST_CASE("external auth: a message while verifying is a handshake error") {
	ExternalAuthFsm t;
	t.build();
	t.feed(proto::C2SHello{ vb::kEngineProtocolVersion, 1, "t" });
	t.feed(proto::C2SAuth{ "x", "jwt" });
	auto step = t.feed(proto::C2SAssetManifestRequest{});
	CHECK(step.disconnect);
}

TEST_CASE("external auth: client obtains a token from the challenge") {
	std::string seen_nonce;
	HandshakeClientConfig cc;
	cc.player_name = "Player";
	vb::net::HandshakeClientHost chost;
	chost.obtain_token = [&](const proto::S2CAuthChallenge &c) {
		seen_nonce = c.nonce;
		return std::optional<std::string>("jwt-from-idp");
	};
	ClientHandshake client(cc, chost);
	client.start();

	auto frame_of = [](auto msg) {
		std::vector<std::byte> payload;
		msg.encode(payload);
		std::vector<std::byte> frame;
		proto::write_frame(frame, decltype(msg)::kType, payload);
		return frame;
	};
	proto::S2CServerInfo info;
	info.engine_protocol_version = vb::kEngineProtocolVersion;
	info.auth_mode = proto::AuthMode::kExternal;
	auto f1 = frame_of(info);
	std::size_t consumed = 0;
	auto p1 = proto::read_frame(span_of(f1), consumed);
	REQUIRE(p1);
	CHECK(client.on_frame(*p1).send.empty()); // waits for the challenge
	CHECK(client.status() == ClientHandshakeStatus::kAwaitingChallenge);

	proto::S2CAuthChallenge ch;
	ch.nonce = "n-xyz";
	auto f2 = frame_of(ch);
	auto p2 = proto::read_frame(span_of(f2), consumed);
	REQUIRE(p2);
	auto step = client.on_frame(*p2);
	REQUIRE(step.send.size() == 1);
	CHECK(seen_nonce == "n-xyz");
	CHECK(client.status() == ClientHandshakeStatus::kAuthenticating);

	std::size_t c2 = 0;
	auto sent = proto::read_frame(span_of(step.send[0].bytes), c2);
	REQUIRE(sent);
	auto auth = proto::C2SAuth::decode(sent->payload);
	REQUIRE(auth);
	CHECK(auth->token == "jwt-from-idp");

	auto f3 = frame_of(proto::S2CAuthResult{ true, "", "alice" });
	auto p3 = proto::read_frame(span_of(f3), consumed);
	REQUIRE(p3);
	client.on_frame(*p3);
	CHECK(client.resolved_name() == "alice");
	CHECK(client.status() == ClientHandshakeStatus::kAwaitingAssetManifest);
}

TEST_CASE("external auth: client without a token source cancels the join") {
	ClientHandshake client(HandshakeClientConfig{});
	client.start();
	proto::S2CServerInfo info;
	info.engine_protocol_version = vb::kEngineProtocolVersion;
	info.auth_mode = proto::AuthMode::kExternal;
	std::vector<std::byte> payload, frame;
	info.encode(payload);
	proto::write_frame(frame, proto::MessageType::kS2CServerInfo, payload);
	std::size_t consumed = 0;
	auto p = proto::read_frame(span_of(frame), consumed);
	REQUIRE(p);
	client.on_frame(*p);

	payload.clear();
	frame.clear();
	proto::S2CAuthChallenge{}.encode(payload);
	proto::write_frame(frame, proto::MessageType::kS2CAuthChallenge, payload);
	auto p2 = proto::read_frame(span_of(frame), consumed);
	REQUIRE(p2);
	CHECK(client.on_frame(*p2).failed);
}
