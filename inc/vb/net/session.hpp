#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <memory>
#include <unordered_map>
#include <utility>

#include "vb/core/ids.hpp"
#include "vb/core/math.hpp"
#include "vb/net/handshake.hpp"
#include "vb/net/transport.hpp"
#include "vb/net/world_replicator.hpp"
#include "vb/physics/movement.hpp"
#include "vb/protocol/handshake.hpp"
#include "vb/protocol/input.hpp"
#include "vb/protocol/snapshot.hpp"
#include "vb/replication/interest.hpp"
#include "vb/world/client_chunk_store.hpp"

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

	// Movement tunables applied to every player (spec §7.3). Set before players
	// join; a Lua pack overrides per entity kind in Phase 4.
	void set_move_params(physics::MoveParams p) { move_params_ = p; }

	// Authoritative feet position of a player (for tests / teleports). Input
	// batches are the normal drive path.
	const physics::MoveState *player_move_state(core::NetId id) const;

	// Optional: attach world replication (chunk streaming). Without it the
	// session only replicates entities.
	void set_world_replicator(std::unique_ptr<WorldReplicator> replicator) {
		replicator_ = std::move(replicator);
	}
	WorldReplicator *world_replicator() { return replicator_.get(); }

private:
	struct Conn {
		explicit Conn(ServerHandshake hs) : handshake(std::move(hs)) {}
		ServerHandshake handshake;
		double age = 0.0;
		bool playing = false;
		bool input_driven = false;
		core::NetId net_id = core::NetId::kInvalid;
		std::vector<core::NetId> last_visible;
		physics::MoveState move;
		core::Vec2f look;
		std::uint32_t last_input_seq = 0;
	};

	void drop(ConnId conn, const std::string &reason);
	const world::BlockSolidQuery &world_query() const;
	void handle_input_batch(Conn &conn, const protocol::C2SInputBatch &batch);
	void handle_block_edit(ConnId conn, Conn &state,
			const protocol::Frame &frame);
	void broadcast_snapshots();
	void broadcast_world();

	Transport &transport_;
	HandshakeServerConfig config_;
	HandshakeServerHost host_;
	std::map<ConnId, Conn> conns_;
	replication::InterestGrid interest_;
	std::unique_ptr<WorldReplicator> replicator_;
	physics::MoveParams move_params_;
	int interest_radius_cells_ = 2;
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
	// `conn` is the ConnId returned by Transport::connect().
	ClientSession(Transport &transport, ConnId conn,
			HandshakeClientConfig config);

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

private:
	// Handle a post-join gameplay message (snapshot / chunk). Returns true if
	// consumed.
	bool apply_gameplay_frame(const protocol::Frame &frame);
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

	physics::MoveState predicted_;
	physics::MoveParams move_params_;
	std::vector<protocol::InputCmd> history_; // unacked, ascending seq
	std::uint32_t last_acked_seq_ = 0;
	std::vector<PendingEdit> pending_edits_;
};

} // namespace vb::net
