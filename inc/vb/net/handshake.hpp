#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "vb/assetsync/manifest.hpp" // assetsync::Manifest
#include "vb/core/ids.hpp"
#include "vb/core/math.hpp"
#include "vb/protocol/assetsync.hpp" // AssetEntryRecord, S2CAssetData
#include "vb/protocol/handshake.hpp"
#include "vb/protocol/message.hpp"
#include "vb/protocol/world.hpp" // BlockRegistryRecord

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
	kAwaitingAssetManifestRequest, // spec §9: sent AuthResult(ok), awaiting C2S_AssetManifestRequest
	kAwaitingAssetRequest, // sent S2C_AssetManifest, awaiting C2S_AssetRequest
	kStreamingAssets, // sending S2C_AssetData across ticks; no frame expected here
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
	std::uint64_t world_seed = 0; // used for JoinAccept when the host grant is 0
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

	// Snapshot of the world's current block registry, sent as
	// S2C_BlockRegistry between C2S_Ready and S2C_JoinAccept (spec §8.3,
	// Phase 4.3). `nullopt` (default) sends no frame at all -- the client
	// already assumes vb::world::BlockRegistry::base() until told otherwise,
	// so hosts/tests that don't care about this get zero behavior change.
	std::function<std::optional<std::vector<protocol::BlockRegistryRecord>>()>
			block_registry = [] {
		return std::optional<std::vector<protocol::BlockRegistryRecord>>{};
	};

	// Effective physics::MoveParams, sent as S2C_MoveParams alongside
	// block_registry above (spec §7.3, Phase 6.7) so client-side prediction
	// uses the exact same tunables as the server's authoritative simulation
	// instead of silently drifting from vb::physics::MoveParams's hardcoded
	// defaults. `nullopt` (default) sends no frame at all -- the client keeps
	// whatever MoveParams it was already constructed with, so hosts/tests
	// that don't care about this see zero behavior change.
	std::function<std::optional<protocol::S2CMoveParams>()> move_params = [] {
		return std::optional<protocol::S2CMoveParams>{};
	};

	// Pack-registered custom keybind names, sent as S2C_KeybindRegistry
	// alongside block_registry above (spec §10.6, Phase 6.3) -- `names[i]`
	// becomes bit i of every InputCmd::keybinds from then on. `nullopt`
	// (default) sends no frame at all: no host/test that doesn't use this
	// channel sees any behavior change.
	std::function<std::optional<std::vector<std::string>>()> keybind_registry =
			[] { return std::optional<std::vector<std::string>>{}; };

	// Built once at server startup (assetsync::build_manifest over the
	// content pack) and handed to every connection by reference -- never
	// rebuilt per connection. `nullptr` (default) skips asset sync entirely
	// (also what a VB_WITH_COMPRESSION-disabled build gets): the server
	// replies with an empty S2C_AssetManifest and moves straight on.
	std::function<std::shared_ptr<const assetsync::Manifest>()> asset_manifest =
			[] { return std::shared_ptr<const assetsync::Manifest>{}; };

	// Raw bytes of one pack file by hash, called lazily once per hash a
	// client actually requests (the common reconnect case -- nothing
	// missing -- never touches disk). `nullopt` should only happen for a
	// hash outside the manifest we just sent (client misbehavior/corruption)
	// and is treated as a hard disconnect.
	std::function<std::optional<std::vector<std::byte>>(core::AssetHash)>
			asset_file_bytes = [](core::AssetHash) { return std::nullopt; };
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
	// Valid once state() == kPlaying: the grant sent in S2C_JoinAccept.
	const JoinGrant &grant() const { return grant_; }

	// Feed one decoded frame from this connection.
	ServerHandshakeStep on_frame(const protocol::Frame &frame);

	// Call when `elapsed_seconds` since connect exceeds the timeout.
	ServerHandshakeStep on_timeout();

	// Call once per tick (not just when a frame arrives) while
	// state() == kStreamingAssets: sends up to `max_chunks` more
	// S2C_AssetData frames (spec §9.3's per-tick pacing), transitioning to
	// kAwaitingReady once every requested hash has been fully sent. A no-op
	// step in any other state.
	ServerHandshakeStep pump_assets(int max_chunks);

private:
	ServerHandshakeStep fail(protocol::DisconnectReason reason,
			const std::string &human_message);

	HandshakeServerConfig config_;
	HandshakeServerHost host_;
	ServerHandshakeState state_ = ServerHandshakeState::kAwaitingHello;
	std::string player_name_;
	JoinGrant grant_;

	std::shared_ptr<const assetsync::Manifest> manifest_;
	struct AssetStreamState {
		std::vector<core::AssetHash> pending;
		std::size_t pending_idx = 0;
		std::vector<std::byte> current_bytes; // fetched lazily via host_.asset_file_bytes
		std::uint32_t current_chunk_idx = 0;
		std::uint32_t current_total_chunks = 0;
		bool current_loaded = false;
	};
	AssetStreamState asset_stream_;
};

// ---------------------------------------------------------------------------
// Client
// ---------------------------------------------------------------------------

enum class ClientHandshakeStatus : std::uint8_t {
	kConnecting, // sent Hello, awaiting ServerInfo
	kAuthenticating, // sent Auth, awaiting AuthResult
	kAwaitingAssetManifest, // sent C2S_AssetManifestRequest, awaiting S2C_AssetManifest
	kSyncingAssets, // sent C2S_AssetRequest, receiving S2C_AssetData frames
	kSyncing, // sent Ready, awaiting JoinAccept
	kJoined,
	kFailed,
};

// Asset-sync side of the client handshake (parallel to HandshakeServerHost).
// ClientHandshake has no filesystem access itself -- these hooks push the
// actual cache mechanics (compute-missing / verify / assemble) out to
// ClientSession's ClientAssetCache. Defaults behave as "I already have
// everything" (asset sync is skipped), so every existing
// ClientHandshake(config) call site keeps compiling unchanged.
struct HandshakeClientHost {
	std::function<core::AssetHash()> last_known_manifest_hash = [] {
		return core::AssetHash{};
	};
	std::function<std::vector<core::AssetHash>(
			const std::vector<protocol::AssetEntryRecord> &)>
			assets_missing = [](const std::vector<protocol::AssetEntryRecord> &) {
		return std::vector<core::AssetHash>{};
	};
	std::function<bool(const protocol::S2CAssetData &)> on_asset_chunk =
			[](const protocol::S2CAssetData &) { return true; };
	std::function<bool()> assets_all_received = [] { return true; };
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
	explicit ClientHandshake(HandshakeClientConfig config,
			HandshakeClientHost host = {});

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
	HandshakeClientHost host_;
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
