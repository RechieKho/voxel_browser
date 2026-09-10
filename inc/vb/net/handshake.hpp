#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "vb/core/ids.hpp"
#include "vb/core/math.hpp"
#include "vb/protocol/handshake.hpp"
#include "vb/protocol/message.hpp"

// Connection handshake state machines (spec §8.3), transport-agnostic: feed them
// decoded frames, get back frames to send plus a terminal outcome. The server
// runs one ServerHandshake per inbound connection; the client runs one
// ClientHandshake for its outbound connection.
//
// Phase 1 covers Hello -> ServerInfo -> Auth -> AuthResult -> Ready ->
// JoinAccept. The asset-manifest and block-registry steps (§8.3, §9) slot in
// between Auth and Ready in Phase 4.

namespace vb::net {

struct OutgoingFrame {
	protocol::Lane lane = protocol::Lane::kControl;
	std::vector<std::byte> bytes;
};

// ---------------------------------------------------------------------------
// Server
// ---------------------------------------------------------------------------

enum class ServerHandshakeState : std::uint8_t {
	kAwaitingHello,
	kAwaitingAuth,
	kAwaitingReady,
	kPlaying,
	kClosed,
};

struct HandshakeServerConfig {
	std::string pack_name = "base";
	std::string pack_version = "0.0.0";
	std::uint16_t tick_rate = 20;
	std::string motd;
	protocol::AuthMode auth_mode = protocol::AuthMode::kNone;
	std::uint32_t max_players = 16;
	double handshake_timeout_seconds = 10.0;
};

struct AuthOutcome {
	bool ok = true;
	std::string reason;
};

struct JoinGrant {
	core::NetId net_id = core::NetId::kInvalid;
	core::Vec3d spawn_pos{ 0.0, 64.0, 0.0 };
	std::uint64_t world_seed = 0;
	std::uint32_t time_of_day = 0;
};

// Host hooks the FSM calls back into. Defaults make auth_mode=none "just work".
struct HandshakeServerHost {
	std::function<std::uint32_t()> current_player_count = [] { return 0u; };
	std::function<AuthOutcome(std::string_view name, std::string_view token)>
			authenticate =
					[](std::string_view name, std::string_view) -> AuthOutcome {
		if (name.empty() || name.size() > 32) {
			return { false, "invalid player name" };
		}
		return { true, {} };
	};
	std::function<JoinGrant(std::string_view name)> on_ready =
			[](std::string_view) { return JoinGrant{}; };
};

struct ServerHandshakeStep {
	std::vector<OutgoingFrame> send;
	bool completed = false; // promote the connection to Playing
	bool disconnect = false;
	protocol::DisconnectReason disconnect_reason =
			protocol::DisconnectReason::kUnknown;
	std::string player_name; // valid once completed
};

class ServerHandshake {
public:
	ServerHandshake(HandshakeServerConfig config, HandshakeServerHost host);

	ServerHandshakeState state() const { return state_; }
	const std::string &player_name() const { return player_name_; }

	// Feed one decoded frame from this connection.
	ServerHandshakeStep on_frame(const protocol::Frame &frame);

	// Call when `elapsed_seconds` since connect exceeds the timeout.
	ServerHandshakeStep on_timeout();

private:
	ServerHandshakeStep fail(protocol::DisconnectReason reason,
			const std::string &human_message);

	HandshakeServerConfig config_;
	HandshakeServerHost host_;
	ServerHandshakeState state_ = ServerHandshakeState::kAwaitingHello;
	std::string player_name_;
};

// ---------------------------------------------------------------------------
// Client
// ---------------------------------------------------------------------------

enum class ClientHandshakeStatus : std::uint8_t {
	kConnecting, // sent Hello, awaiting ServerInfo
	kAuthenticating, // sent Auth, awaiting AuthResult
	kSyncing, // sent Ready, awaiting JoinAccept
	kJoined,
	kFailed,
};

struct HandshakeClientConfig {
	std::string player_name = "Player";
	std::string token;
	std::string client_version = "voxel_browser";
	std::uint64_t client_nonce = 0;
};

struct ClientHandshakeStep {
	std::vector<OutgoingFrame> send;
	bool completed = false;
	bool failed = false;
	std::string failure_reason;
};

class ClientHandshake {
public:
	explicit ClientHandshake(HandshakeClientConfig config);

	ClientHandshakeStatus status() const { return status_; }

	// Frames to send immediately once the transport reports Connected.
	ClientHandshakeStep start();

	ClientHandshakeStep on_frame(const protocol::Frame &frame);

	const std::optional<protocol::S2CServerInfo> &server_info() const {
		return server_info_;
	}
	const std::optional<protocol::S2CJoinAccept> &join_accept() const {
		return join_accept_;
	}

private:
	ClientHandshakeStep fail(std::string reason);

	HandshakeClientConfig config_;
	ClientHandshakeStatus status_ = ClientHandshakeStatus::kConnecting;
	std::optional<protocol::S2CServerInfo> server_info_;
	std::optional<protocol::S2CJoinAccept> join_accept_;
};

// Shared helper: frame a message into an OutgoingFrame on its natural lane.
template <typename Msg>
OutgoingFrame frame_message(const Msg &msg, std::uint16_t flags = 0) {
	std::vector<std::byte> payload;
	msg.encode(payload);
	OutgoingFrame out;
	out.lane = protocol::lane_for(Msg::kType);
	protocol::write_frame(out.bytes, Msg::kType, payload, flags);
	return out;
}

} // namespace vb::net
