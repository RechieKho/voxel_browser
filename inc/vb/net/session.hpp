#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <memory>
#include <unordered_map>
#include <utility>

#include "vb/assetsync/cache.hpp"
#include "vb/core/ids.hpp"
#include "vb/core/math.hpp"
#include "vb/net/handshake.hpp"
#include "vb/net/transport.hpp"
#include "vb/net/world_replicator.hpp"
#include "vb/physics/movement.hpp"
#include "vb/protocol/chat.hpp"
#include "vb/protocol/handshake.hpp"
#include "vb/protocol/input.hpp"
#include "vb/protocol/inventory.hpp"
#include "vb/protocol/snapshot.hpp"
#include "vb/replication/interest.hpp"
#include "vb/world/client_chunk_store.hpp"
#include "vb/world/item_drops.hpp"

// Sessions glue a Transport to the handshake FSMs and present a small
// game-facing API: the server loop pulls join/leave events, the client loop
// polls a status. Both are driven by one tick() per frame/tick.
//
// Transport backend is injected, so the same ServerSession runs behind a
// LoopbackTransport (integrated singleplayer, tests) or a GnsTransport
// (dedicated server) with no code change.

namespace vb::net {

// --- server ---------------------------------------------------------------

struct SessionPlayerJoined {
	ConnId conn = ConnId::kInvalid;
	core::NetId net_id = core::NetId::kInvalid;
	std::string name;
};

struct SessionPlayerLeft {
	ConnId conn = ConnId::kInvalid;
	core::NetId net_id = core::NetId::kInvalid;
	std::string reason;
};

class ServerSession {
public:
	ServerSession(Transport &transport, HandshakeServerConfig config,
			HandshakeServerHost host = {});

	void tick(double dt_seconds);

	std::vector<SessionPlayerJoined> take_joins();
	std::vector<SessionPlayerLeft> take_leaves();

	std::size_t player_count() const { return playing_; }
	std::size_t pending_count() const { return conns_.size() - playing_; }
	std::uint32_t server_tick() const { return server_tick_; }

	// Feed authoritative entity state into the replication grid (Phase 3's
	// movement systems will call this; tests set it directly).
	void set_player_state(core::NetId id, core::Vec3d pos, core::Vec2f rot = {},
			core::Vec3f vel = {});

	void set_interest_radius_cells(int cells) { interest_radius_cells_ = cells; }

	// Day/night cycle (spec §5.4): real seconds for one full in-game day.
	// Default matches world::advance_time_of_day's expectations; set before
	// players join if a pack/host wants a different pace.
	void set_day_length_seconds(double seconds) { day_length_seconds_ = seconds; }
	std::uint32_t time_of_day() const {
		return static_cast<std::uint32_t>(time_of_day_ticks_);
	}

	// Movement tunables applied to every player (spec §7.3). Set before players
	// join; a Lua pack overrides per entity kind in Phase 4.
	void set_move_params(physics::MoveParams p) { move_params_ = p; }

	// Authoritative feet position of a player (for tests / teleports). Input
	// batches are the normal drive path.
	const physics::MoveState *player_move_state(core::NetId id) const;

	// Phase 4.2 (Lua entity/player API): directly set a connected player's
	// authoritative velocity. No-op if `id` isn't a playing connection.
	void set_player_velocity(core::NetId id, core::Vec3d vel);

	// Transport-level connection for a playing net id (kInvalid if not
	// found/not playing) — lets a script host send arbitrary framed messages
	// (chat / open_ui) without ServerSession knowing their contents.
	ConnId conn_for_player(core::NetId id) const;

	// Display name of a playing net id ("" if not found/not playing).
	std::string_view player_name(core::NetId id) const;

	// Optional: attach world replication (chunk streaming). Without it the
	// session only replicates entities.
	void set_world_replicator(std::unique_ptr<WorldReplicator> replicator) {
		replicator_ = std::move(replicator);
	}
	WorldReplicator *world_replicator() { return replicator_.get(); }

	// Phase 4.5: routes a playing connection's C2S_UiEvent up to a script
	// host, without ServerSession knowing anything about Lua. Unset (the
	// default) leaves UI events silently ignored, same posture as the
	// "unknown post-join message" comment this replaces for kC2SUiEvent.
	void set_ui_event_handler(
			std::function<void(core::NetId, const protocol::C2SUiEvent &)> handler) {
		on_ui_event_ = std::move(handler);
	}

	// Phase 5.4: routes a playing connection's C2S_Chat up to a script host as
	// a veto (return false to suppress) before ServerSession broadcasts it.
	// Unset (the default -- e.g. `--singleplayer`, which has no PackRuntime)
	// means every chat line is allowed.
	void set_chat_handler(
			std::function<bool(core::NetId, std::string_view)> handler) {
		on_chat_ = std::move(handler);
	}

	// Death/respawn (spec §5.4): a player whose feet fall below this world Y
	// (the "void") is killed instantly and respawned at their spawn point.
	// Health also generically triggers a respawn at 0 -- nothing decrements
	// it yet besides the void check (no combat system exists), but the path
	// is shared so a future damage source gets respawn for free.
	void set_void_kill_y(double y) { void_kill_y_ = y; }

	// Dropped-item entities (spec §5.1). Spawns one at `pos`, replicated
	// generically through the interest grid like any other entity -- no
	// dedicated wire message. Auto-collected when a player's feet come
	// within the system's pickup radius (see ItemDropSystem); the pickup
	// handler below is how that reaches a player's actual inventory.
	core::NetId spawn_item_drop(
			core::Vec3d pos, core::BlockId item, std::uint16_t count);

	// Phase 5.1: routes a player walking over a dropped item up to a script
	// host's inventory, without ServerSession knowing anything about Lua.
	// Unset (the default -- e.g. `--singleplayer`, which has no PackRuntime)
	// means picked-up items vanish with no effect, same "no handler, no
	// side effect" posture as set_chat_handler/set_ui_event_handler.
	void set_item_pickup_handler(
			std::function<void(core::NetId, core::BlockId, std::uint16_t)> handler) {
		on_item_pickup_ = std::move(handler);
	}

private:
	struct Conn {
		explicit Conn(ServerHandshake hs) : handshake(std::move(hs)) {}
		ServerHandshake handshake;
		double age = 0.0;
		bool playing = false;
		bool input_driven = false;
		core::NetId net_id = core::NetId::kInvalid;
		std::string name;
		std::vector<core::NetId> last_visible;
		physics::MoveState move;
		core::Vec2f look;
		std::uint32_t last_input_seq = 0;
		core::Vec3d spawn_pos{};
		float health = 20.0f;
	};

	void drop(ConnId conn, const std::string &reason);
	const world::BlockSolidQuery &world_query() const;
	void handle_input_batch(Conn &conn, const protocol::C2SInputBatch &batch);
	void handle_block_edit(ConnId conn, Conn &state,
			const protocol::Frame &frame);
	void handle_chat(Conn &state, const protocol::Frame &frame);
	void check_respawns();
	void update_item_drops(double dt_seconds);
	void broadcast_snapshots();
	void broadcast_world();
	void broadcast_time_of_day();

	Transport &transport_;
	HandshakeServerConfig config_;
	HandshakeServerHost host_;
	std::map<ConnId, Conn> conns_;
	replication::InterestGrid interest_;
	std::unique_ptr<WorldReplicator> replicator_;
	std::function<void(core::NetId, const protocol::C2SUiEvent &)> on_ui_event_;
	std::function<bool(core::NetId, std::string_view)> on_chat_;
	world::ItemDropSystem item_drops_;
	std::function<void(core::NetId, core::BlockId, std::uint16_t)> on_item_pickup_;
	physics::MoveParams move_params_;
	int interest_radius_cells_ = 2;
	double time_of_day_ticks_ = 0.0;
	double day_length_seconds_ = 1200.0; // 20 real minutes per in-game day
	double time_of_day_broadcast_accum_ = 0.0;
	double void_kill_y_ = -64.0;
	std::uint32_t server_tick_ = 0;
	std::size_t playing_ = 0;
	std::uint32_t next_net_id_ = 1;
	std::vector<TransportEvent> scratch_;
	std::vector<SessionPlayerJoined> joins_;
	std::vector<SessionPlayerLeft> leaves_;
};

// --- client --------------------------------------------------------------

class ClientSession {
public:
	// `conn` is the ConnId returned by Transport::connect(). `cache` is
	// optional (default nullptr): when supplied, ClientHandshake's asset-sync
	// hooks are wired to it for real (Phase 4.4); when null, asset sync
	// behaves as already-synced (ClientHandshake's own no-op defaults).
	ClientSession(Transport &transport, ConnId conn, HandshakeClientConfig config,
			assetsync::ClientAssetCache *cache = nullptr);

	void tick(double dt_seconds);

	ClientHandshakeStatus status() const { return handshake_.status(); }
	bool joined() const {
		return handshake_.status() == ClientHandshakeStatus::kJoined;
	}
	bool failed() const {
		return handshake_.status() == ClientHandshakeStatus::kFailed ||
				!failure_reason_.empty();
	}
	const std::string &failure_reason() const { return failure_reason_; }

	const std::optional<protocol::S2CServerInfo> &server_info() const {
		return handshake_.server_info();
	}
	const std::optional<protocol::S2CJoinAccept> &join_accept() const {
		return handshake_.join_accept();
	}

	// Replicated view of other entities (spec §8.4). Updated from every
	// S2C_EntitySnapshot once joined.
	const std::unordered_map<core::NetId, protocol::EntityRecord> &
	remote_entities() const {
		return remote_;
	}
	std::uint32_t last_server_tick() const { return last_server_tick_; }

	// --- local-player prediction (spec §8.4) -----------------------------

	void set_move_params(physics::MoveParams p) { move_params_ = p; }
	// Seed the predicted state from S2C_JoinAccept spawn_pos.
	void set_local_feet(core::Vec3d feet) { predicted_.position = feet; }

	// Sample one input: predict locally against the chunk mirror, keep it in the
	// unacked history, and send a batch of recent commands on lane 4.
	void push_input(const protocol::InputCmd &cmd);

	// --- block editing (spec §5.2) --------------------------------------

	// Optimistically apply an edit to the local chunk mirror, remember it for
	// rollback, and send it. The server confirms with S2C_BlockEditResult and
	// the authoritative S2C_ChunkDelta.
	void push_block_edit(const protocol::C2SBlockEdit &edit);
	std::size_t pending_edit_count() const { return pending_edits_.size(); }

	const physics::MoveState &predicted_state() const { return predicted_; }
	core::Vec3d predicted_feet() const { return predicted_.position; }
	std::uint32_t last_acked_input_seq() const { return last_acked_seq_; }
	std::size_t unacked_input_count() const { return history_.size(); }

	// Remote entity position for rendering, interpolated at
	// server_time_est - 100 ms (spec §8.4). Falls back to the raw snapshot pos
	// when only one sample is known.
	core::Vec3d interpolated_pos(core::NetId id) const;

	// Replicated chunk mirror (spec §11.2), populated from S2C_Chunk* messages.
	const world::ClientChunkStore &chunk_store() const { return chunks_; }
	world::ClientChunkStore &chunk_store() { return chunks_; }

	// Virtual pack filesystem assembled by the asset-sync cache (Phase 4.4),
	// path -> bytes. Empty if no cache was supplied or sync hasn't finished.
	// Nothing consumes this yet (Lua require / texture loader land later).
	const std::unordered_map<std::string, std::vector<std::byte>> &
	virtual_pack_fs() const;

	// --- client UI VM (spec §10.4, Phase 4.5) ---------------------------

	// Drains a pending S2C_OpenUi, if one arrived since the last call.
	std::optional<protocol::S2COpenUi> take_open_ui();

	// Sends one C2S_UiEvent (a UiRuntime widget callback calling
	// ui.send_event/ui.close). No optimistic local state, unlike block
	// edits -- just a pass-through RPC to the server's Lua VM.
	void send_ui_event(const protocol::C2SUiEvent &event) {
		send_message(transport_, conn_, event);
	}

	// --- chat (spec §5.4) -------------------------------------------------

	// Sends one C2S_Chat line typed into the HUD chat box.
	void send_chat(std::string_view text) {
		send_message(transport_, conn_, protocol::C2SChat{ std::string(text) });
	}

	// Drains chat lines (already server-formatted "<name>: <text>") received
	// since the last call, oldest first. Join/leave notices also land here as
	// "* <name> joined/left the game" lines (spec §5.4's "join-leave
	// messages"), interleaved with real chat in receipt order.
	std::vector<std::string> take_chat_messages();

	// Everyone else currently known to be playing (net id -> display name),
	// kept in sync by S2C_PlayerList/S2C_PlayerJoin/S2C_PlayerLeave. Does not
	// include this client's own name.
	const std::unordered_map<core::NetId, std::string> &players() const {
		return players_;
	}

	// This player's inventory (spec §5.1), kept in sync by S2C_Inventory.
	// Empty until the first snapshot arrives (e.g. before any player:give()).
	const std::vector<protocol::InventorySlot> &inventory() const {
		return inventory_;
	}

	// Day/night cycle (spec §5.4): S2C_JoinAccept's value until the first
	// periodic S2C_TimeOfDay update arrives, then the latest of those. Ticks
	// into the day cycle -- see vb::world::daynight.hpp for the convention.
	std::uint32_t time_of_day() const {
		if (time_of_day_override_) {
			return *time_of_day_override_;
		}
		if (join_accept()) {
			return join_accept()->time_of_day;
		}
		return 0;
	}

private:
	// Handle a post-join gameplay message (snapshot / chunk). Returns true if
	// consumed.
	bool apply_gameplay_frame(const protocol::Frame &frame);
	void apply_block_registry(const protocol::S2CBlockRegistry &msg);
	void apply_snapshot(const protocol::S2CEntitySnapshot &snap);
	void reconcile(const protocol::EntityRecord &authoritative,
			std::uint32_t acked_seq);
	void handle_block_edit_result(const protocol::S2CBlockEditResult &res);
	void forget_pending_edits_for(core::ChunkCoord coord);

	struct PendingEdit {
		std::uint32_t seq = 0;
		core::IVec3 pos{};
		core::BlockId prev = core::BlockId::kAir;
		core::ChunkCoord coord{};
	};

	struct RemoteSample {
		core::Vec3d prev_pos{};
		core::Vec3d cur_pos{};
		std::uint32_t prev_tick = 0;
		std::uint32_t cur_tick = 0;
	};

	Transport &transport_;
	ConnId conn_;
	ClientHandshake handshake_;
	bool started_ = false;
	std::string failure_reason_;
	std::vector<TransportEvent> scratch_;
	std::unordered_map<core::NetId, protocol::EntityRecord> remote_;
	std::unordered_map<core::NetId, RemoteSample> remote_samples_;
	world::ClientChunkStore chunks_{ world::BlockRegistry::base() };
	std::uint32_t last_server_tick_ = 0;
	assetsync::ClientAssetCache *asset_cache_ = nullptr; // not owned; may be null
	std::optional<protocol::S2COpenUi> pending_open_ui_;
	std::vector<std::string> pending_chat_;
	std::unordered_map<core::NetId, std::string> players_;
	std::optional<std::uint32_t> time_of_day_override_;
	std::vector<protocol::InventorySlot> inventory_;

	physics::MoveState predicted_;
	physics::MoveParams move_params_;
	std::vector<protocol::InputCmd> history_; // unacked, ascending seq
	std::uint32_t last_acked_seq_ = 0;
	std::vector<PendingEdit> pending_edits_;
};

} // namespace vb::net
