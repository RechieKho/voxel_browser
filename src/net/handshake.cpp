#include "vb/net/handshake.hpp"

#include <algorithm>
#include <utility>

#include "vb/core/log.hpp"
#include "vb/core/version.hpp"
#include "vb/protocol/input.hpp" // S2CKeybindRegistry

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

constexpr std::size_t kAssetChunkBytes = 48u * 1024u;

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

ServerHandshakeStep ServerHandshake::pump_assets(int max_chunks) {
	ServerHandshakeStep step;
	if (state_ != ServerHandshakeState::kStreamingAssets) {
		return step;
	}

	for (int sent = 0; sent < max_chunks; ++sent) {
		if (asset_stream_.pending_idx >= asset_stream_.pending.size()) {
			state_ = ServerHandshakeState::kAwaitingReady;
			return step;
		}
		const core::AssetHash hash = asset_stream_.pending[asset_stream_.pending_idx];

		if (!asset_stream_.current_loaded) {
			auto bytes = host_.asset_file_bytes(hash);
			if (!bytes) {
				// Client asked for a hash outside the manifest we sent it --
				// misbehavior or corruption, not recoverable.
				return fail(DisconnectReason::kProtocolError,
						"requested asset hash not found");
			}
			asset_stream_.current_bytes = std::move(*bytes);
			asset_stream_.current_chunk_idx = 0;
			const std::size_t size = asset_stream_.current_bytes.size();
			asset_stream_.current_total_chunks = static_cast<std::uint32_t>(
					size == 0 ? 1 : (size + kAssetChunkBytes - 1) / kAssetChunkBytes);
			asset_stream_.current_loaded = true;
		}

		const std::size_t offset =
				static_cast<std::size_t>(asset_stream_.current_chunk_idx) * kAssetChunkBytes;
		const std::size_t remaining = asset_stream_.current_bytes.size() - offset;
		const std::size_t chunk_len = std::min(remaining, kAssetChunkBytes);

		protocol::S2CAssetData data;
		data.hash = hash;
		data.seq = asset_stream_.current_chunk_idx;
		data.total_chunks = asset_stream_.current_total_chunks;
		data.bytes.assign(asset_stream_.current_bytes.begin() + static_cast<std::ptrdiff_t>(offset),
				asset_stream_.current_bytes.begin() + static_cast<std::ptrdiff_t>(offset + chunk_len));
		step.send.push_back(frame_message(data));

		++asset_stream_.current_chunk_idx;
		if (asset_stream_.current_chunk_idx >= asset_stream_.current_total_chunks) {
			++asset_stream_.pending_idx;
			asset_stream_.current_bytes.clear();
			asset_stream_.current_loaded = false;
		}
	}
	return step;
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
			state_ = ServerHandshakeState::kAwaitingAssetManifestRequest;
			return step;
		}

		case ServerHandshakeState::kAwaitingAssetManifestRequest: {
			if (type != MessageType::kC2SAssetManifestRequest) {
				return fail(DisconnectReason::kBadHandshake,
						"expected AssetManifestRequest");
			}
			auto req = protocol::C2SAssetManifestRequest::decode(frame.payload);
			if (!req) {
				return fail(DisconnectReason::kProtocolError,
						"malformed AssetManifestRequest");
			}

			manifest_ = host_.asset_manifest(); // may be null (opt-out / disabled)
			protocol::S2CAssetManifest reply;
			if (manifest_) {
				reply.manifest_hash = manifest_->manifest_hash;
				reply.total_bytes = manifest_->total_bytes;
				if (req->known_manifest_hash != manifest_->manifest_hash) {
					reply.entries.reserve(manifest_->entries.size());
					for (const auto &e : manifest_->entries) {
						reply.entries.push_back({ e.path, e.hash, e.size,
								static_cast<protocol::AssetKind>(e.kind) });
					}
				}
				// else: reconnect fast path -- entries stay empty, client
				// already has everything for this manifest_hash.
			}

			state_ = ServerHandshakeState::kAwaitingAssetRequest;
			ServerHandshakeStep step;
			step.send.push_back(frame_message(reply));
			return step;
		}

		case ServerHandshakeState::kAwaitingAssetRequest: {
			if (type != MessageType::kC2SAssetRequest) {
				return fail(DisconnectReason::kBadHandshake, "expected AssetRequest");
			}
			auto req = protocol::C2SAssetRequest::decode(frame.payload);
			if (!req) {
				return fail(DisconnectReason::kProtocolError, "malformed AssetRequest");
			}

			asset_stream_ = AssetStreamState{};
			asset_stream_.pending = std::move(req->missing);
			if (asset_stream_.pending.empty()) {
				state_ = ServerHandshakeState::kAwaitingReady;
				return {};
			}
			state_ = ServerHandshakeState::kStreamingAssets;
			return {}; // first bytes go out from the next pump_assets() tick
		}

		case ServerHandshakeState::kStreamingAssets:
			// No client message is expected while streaming; the server
			// paces itself via pump_assets(), called from ServerSession's
			// tick loop, not in response to a frame.
			return fail(DisconnectReason::kBadHandshake,
					"unexpected message while streaming assets");

		case ServerHandshakeState::kAwaitingReady: {
			if (type != MessageType::kC2SReady) {
				return fail(DisconnectReason::kBadHandshake, "expected Ready");
			}
			if (!protocol::C2SReady::decode(frame.payload)) {
				return fail(DisconnectReason::kProtocolError, "malformed Ready");
			}

			grant_ = host_.on_ready(player_name_);

			state_ = ServerHandshakeState::kPlaying;
			ServerHandshakeStep step;
			if (auto records = host_.block_registry()) {
				step.send.push_back(
						frame_message(protocol::S2CBlockRegistry{ std::move(*records) }));
			}
			if (auto names = host_.keybind_registry()) {
				step.send.push_back(
						frame_message(protocol::S2CKeybindRegistry{ std::move(*names) }));
			}
			if (auto mp = host_.move_params()) {
				step.send.push_back(frame_message(*mp));
			}

			protocol::S2CJoinAccept accept;
			accept.your_net_id = grant_.net_id;
			accept.spawn_pos = grant_.spawn_pos;
			accept.world_seed = grant_.world_seed;
			accept.time_of_day = grant_.time_of_day;
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

ClientHandshake::ClientHandshake(HandshakeClientConfig config,
		HandshakeClientHost host) : config_(std::move(config)),
									host_(std::move(host)) {}

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

			status_ = ClientHandshakeStatus::kAwaitingAssetManifest;
			ClientHandshakeStep step;
			protocol::C2SAssetManifestRequest req;
			req.known_manifest_hash = host_.last_known_manifest_hash();
			step.send.push_back(frame_message(req));
			return step;
		}

		case ClientHandshakeStatus::kAwaitingAssetManifest: {
			if (type != MessageType::kS2CAssetManifest) {
				return fail("expected AssetManifest");
			}
			auto m = protocol::S2CAssetManifest::decode(frame.payload);
			if (!m) {
				return fail("malformed AssetManifest");
			}
			std::vector<core::AssetHash> missing = host_.assets_missing(m->entries);

			ClientHandshakeStep step;
			step.send.push_back(frame_message(protocol::C2SAssetRequest{ missing }));
			if (missing.empty()) {
				// Nothing to receive (reconnect fast path or an empty pack) --
				// proceed straight to Ready, mirroring the server's shortcut.
				status_ = ClientHandshakeStatus::kSyncing;
				step.send.push_back(frame_message(protocol::C2SReady{}));
			} else {
				status_ = ClientHandshakeStatus::kSyncingAssets;
			}
			return step;
		}

		case ClientHandshakeStatus::kSyncingAssets: {
			if (type != MessageType::kS2CAssetData) {
				return fail("expected AssetData");
			}
			auto d = protocol::S2CAssetData::decode(frame.payload);
			if (!d) {
				return fail("malformed AssetData");
			}
			if (!host_.on_asset_chunk(*d)) {
				return fail("asset transfer failed (hash mismatch or size cap)");
			}
			if (!host_.assets_all_received()) {
				return {}; // more chunks still expected
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
