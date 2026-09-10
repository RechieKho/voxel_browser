#include "vb/net/handshake.hpp"

#include <utility>

#include "vb/core/log.hpp"
#include "vb/core/version.hpp"

namespace vb::net {

using protocol::DisconnectReason;
using protocol::Frame;
using protocol::MessageType;

namespace {

OutgoingFrame disconnect_frame(DisconnectReason reason, std::string message) {
	protocol::S2CDisconnect msg;
	msg.reason = reason;
	msg.message = std::move(message);
	return frame_message(msg);
}

} // namespace

// ===========================================================================
// ServerHandshake
// ===========================================================================

ServerHandshake::ServerHandshake(HandshakeServerConfig config,
		HandshakeServerHost host) : config_(std::move(config)),
									host_(std::move(host)) {}

ServerHandshakeStep ServerHandshake::fail(DisconnectReason reason,
		const std::string &human_message) {
	VB_DEBUG("net", "handshake rejected: ", human_message);
	state_ = ServerHandshakeState::kClosed;
	ServerHandshakeStep step;
	step.send.push_back(disconnect_frame(reason, human_message));
	step.disconnect = true;
	step.disconnect_reason = reason;
	return step;
}

ServerHandshakeStep ServerHandshake::on_timeout() {
	if (state_ == ServerHandshakeState::kPlaying ||
			state_ == ServerHandshakeState::kClosed) {
		return {};
	}
	return fail(DisconnectReason::kTimeout, "handshake timed out");
}

ServerHandshakeStep ServerHandshake::on_frame(const Frame &frame) {
	const MessageType type = frame.header.type;

	switch (state_) {
		case ServerHandshakeState::kAwaitingHello: {
			if (type != MessageType::kC2SHello) {
				return fail(DisconnectReason::kBadHandshake, "expected Hello");
			}
			auto hello = protocol::C2SHello::decode(frame.payload);
			if (!hello) {
				return fail(DisconnectReason::kProtocolError, "malformed Hello");
			}
			if (hello->engine_protocol_version != kEngineProtocolVersion) {
				return fail(DisconnectReason::kProtocolMismatch,
						"engine protocol version mismatch");
			}

			protocol::S2CServerInfo info;
			info.pack_name = config_.pack_name;
			info.pack_version = config_.pack_version;
			info.engine_protocol_version = kEngineProtocolVersion;
			info.tick_rate = config_.tick_rate;
			info.motd = config_.motd;
			info.auth_mode = config_.auth_mode;

			state_ = ServerHandshakeState::kAwaitingAuth;
			ServerHandshakeStep step;
			step.send.push_back(frame_message(info));
			return step;
		}

		case ServerHandshakeState::kAwaitingAuth: {
			if (type != MessageType::kC2SAuth) {
				return fail(DisconnectReason::kBadHandshake, "expected Auth");
			}
			auto auth = protocol::C2SAuth::decode(frame.payload);
			if (!auth) {
				return fail(DisconnectReason::kProtocolError, "malformed Auth");
			}
			if (host_.current_player_count() >= config_.max_players) {
				return fail(DisconnectReason::kServerFull, "server is full");
			}

			const AuthOutcome outcome =
					host_.authenticate(auth->player_name, auth->token);
			ServerHandshakeStep step;
			protocol::S2CAuthResult result;
			result.ok = outcome.ok;
			result.reason = outcome.reason;
			step.send.push_back(frame_message(result));
			if (!outcome.ok) {
				step.send.push_back(disconnect_frame(DisconnectReason::kAuthFailed,
						outcome.reason.empty() ? "authentication failed"
											   : outcome.reason));
				step.disconnect = true;
				step.disconnect_reason = DisconnectReason::kAuthFailed;
				state_ = ServerHandshakeState::kClosed;
				return step;
			}

			player_name_ = auth->player_name;
			state_ = ServerHandshakeState::kAwaitingReady;
			return step;
		}

		case ServerHandshakeState::kAwaitingReady: {
			if (type != MessageType::kC2SReady) {
				return fail(DisconnectReason::kBadHandshake, "expected Ready");
			}
			if (!protocol::C2SReady::decode(frame.payload)) {
				return fail(DisconnectReason::kProtocolError, "malformed Ready");
			}

			grant_ = host_.on_ready(player_name_);
			protocol::S2CJoinAccept accept;
			accept.your_net_id = grant_.net_id;
			accept.spawn_pos = grant_.spawn_pos;
			accept.world_seed = grant_.world_seed;
			accept.time_of_day = grant_.time_of_day;

			state_ = ServerHandshakeState::kPlaying;
			ServerHandshakeStep step;
			step.send.push_back(frame_message(accept));
			step.completed = true;
			step.player_name = player_name_;
			return step;
		}

		case ServerHandshakeState::kPlaying:
		case ServerHandshakeState::kClosed:
			return fail(DisconnectReason::kBadHandshake,
					"unexpected message after handshake");
	}
	return {};
}

// ===========================================================================
// ClientHandshake
// ===========================================================================

ClientHandshake::ClientHandshake(HandshakeClientConfig config) : config_(std::move(config)) {}

ClientHandshakeStep ClientHandshake::fail(std::string reason) {
	status_ = ClientHandshakeStatus::kFailed;
	ClientHandshakeStep step;
	step.failed = true;
	step.failure_reason = std::move(reason);
	return step;
}

ClientHandshakeStep ClientHandshake::start() {
	protocol::C2SHello hello;
	hello.engine_protocol_version = kEngineProtocolVersion;
	hello.client_nonce = config_.client_nonce;
	hello.client_version = config_.client_version;

	status_ = ClientHandshakeStatus::kConnecting;
	ClientHandshakeStep step;
	step.send.push_back(frame_message(hello));
	return step;
}

ClientHandshakeStep ClientHandshake::on_frame(const Frame &frame) {
	const MessageType type = frame.header.type;

	if (type == MessageType::kS2CDisconnect) {
		auto msg = protocol::S2CDisconnect::decode(frame.payload);
		return fail(msg ? msg->message : "disconnected");
	}

	switch (status_) {
		case ClientHandshakeStatus::kConnecting: {
			if (type != MessageType::kS2CServerInfo) {
				return fail("expected ServerInfo");
			}
			auto info = protocol::S2CServerInfo::decode(frame.payload);
			if (!info) {
				return fail("malformed ServerInfo");
			}
			if (info->engine_protocol_version != kEngineProtocolVersion) {
				return fail("engine protocol version mismatch");
			}
			server_info_ = std::move(*info);

			protocol::C2SAuth auth;
			auth.player_name = config_.player_name;
			auth.token = config_.token;

			status_ = ClientHandshakeStatus::kAuthenticating;
			ClientHandshakeStep step;
			step.send.push_back(frame_message(auth));
			return step;
		}

		case ClientHandshakeStatus::kAuthenticating: {
			if (type != MessageType::kS2CAuthResult) {
				return fail("expected AuthResult");
			}
			auto result = protocol::S2CAuthResult::decode(frame.payload);
			if (!result) {
				return fail("malformed AuthResult");
			}
			if (!result->ok) {
				return fail(result->reason.empty() ? "authentication rejected"
												   : result->reason);
			}

			status_ = ClientHandshakeStatus::kSyncing;
			ClientHandshakeStep step;
			step.send.push_back(frame_message(protocol::C2SReady{}));
			return step;
		}

		case ClientHandshakeStatus::kSyncing: {
			if (type != MessageType::kS2CJoinAccept) {
				return fail("expected JoinAccept");
			}
			auto accept = protocol::S2CJoinAccept::decode(frame.payload);
			if (!accept) {
				return fail("malformed JoinAccept");
			}
			join_accept_ = std::move(*accept);

			status_ = ClientHandshakeStatus::kJoined;
			ClientHandshakeStep step;
			step.completed = true;
			return step;
		}

		case ClientHandshakeStatus::kJoined:
		case ClientHandshakeStatus::kFailed:
			return {};
	}
	return {};
}

} // namespace vb::net
