#include "vb/net/session.hpp"

#include <algorithm>
#include <span>
#include <utility>

#include "vb/core/log.hpp"
#include "vb/core/math.hpp"
#include "vb/protocol/message.hpp"
#include "vb/protocol/world.hpp"

namespace vb::net {

namespace {

std::span<const std::byte> span_of(const std::vector<std::byte> &v) {
	return { v.data(), v.size() };
}

// Fallback voxel query when no world replicator is attached (entity-only
// sessions and early tests): everything is open air.
struct EmptyBlockQuery final : world::BlockSolidQuery {
	core::BlockId block_at(core::IVec3) const override {
		return core::BlockId::kAir;
	}
	bool solid_at(core::IVec3) const override { return false; }
};
const EmptyBlockQuery kEmptyBlockQuery;

physics::MoveInput to_move_input(const protocol::InputCmd &c) {
	physics::MoveInput mi;
	mi.dt = core::clamp(static_cast<double>(c.dt), 0.0, 0.1);
	mi.wish_dir = physics::wish_dir_from_local(c.move, c.yaw);
	mi.jump = (c.buttons & protocol::kInputJump) != 0;
	mi.sprint = (c.buttons & protocol::kInputSprint) != 0;
	mi.fly_up = (c.buttons & protocol::kInputFlyUp) != 0;
	mi.fly_down = (c.buttons & protocol::kInputFlyDown) != 0;
	return mi;
}

std::uint8_t pack_flags(bool on_ground) {
	return on_ground ? 1u : 0u;
}

void send_frames(Transport &t, ConnId conn,
		const std::vector<OutgoingFrame> &frames) {
	for (const auto &f : frames) {
		t.send(conn, f.lane, span_of(f.bytes));
	}
}

} // namespace

// ===========================================================================
// ServerSession
// ===========================================================================

ServerSession::ServerSession(Transport &transport, HandshakeServerConfig config,
		HandshakeServerHost host) : transport_(transport),
									config_(std::move(config)),
									host_(std::move(host)) {
	// Session owns the authoritative player count and net-id allocation; wrap
	// whatever the caller passed so their auth/spawn hooks still run.
	host_.current_player_count = [this] {
		return static_cast<std::uint32_t>(playing_);
	};
	// Keep the caller's grant logic, then fill in a net id / seed if it didn't.
	auto user_on_ready = host_.on_ready;
	host_.on_ready = [this, user_on_ready](std::string_view name) {
		JoinGrant grant = user_on_ready ? user_on_ready(name) : JoinGrant{};
		if (grant.net_id == core::NetId::kInvalid) {
			grant.net_id = static_cast<core::NetId>(next_net_id_++);
		}
		if (grant.world_seed == 0) {
			grant.world_seed = config_.world_seed;
		}
		return grant;
	};
}

void ServerSession::drop(ConnId conn, const std::string &reason) {
	auto it = conns_.find(conn);
	if (it == conns_.end()) {
		return;
	}
	transport_.close(conn, reason);
}

const world::BlockSolidQuery &ServerSession::world_query() const {
	if (replicator_) {
		return replicator_->world();
	}
	return kEmptyBlockQuery;
}

void ServerSession::handle_input_batch(Conn &conn,
		const protocol::C2SInputBatch &batch) {
	const world::BlockSolidQuery &world = world_query();
	for (const auto &cmd : batch.cmds) {
		if (cmd.seq <= conn.last_input_seq) {
			continue; // already simulated (batches resend recent commands)
		}
		conn.move = physics::step_movement(conn.move, to_move_input(cmd),
				move_params_, world);
		conn.last_input_seq = cmd.seq;
		conn.look = { cmd.yaw, cmd.pitch };
	}
	conn.input_driven = true;

	replication::EntityState s;
	if (const auto *e = interest_.get(conn.net_id)) {
		s = *e;
	}
	s.net_id = conn.net_id;
	s.pos = conn.move.position;
	s.rot = conn.look;
	s.vel = core::Vec3f{ static_cast<float>(conn.move.velocity.x),
		static_cast<float>(conn.move.velocity.y),
		static_cast<float>(conn.move.velocity.z) };
	interest_.upsert(s);
}

void ServerSession::handle_block_edit(ConnId conn, Conn &state,
		const protocol::Frame &frame) {
	if (!replicator_) {
		return;
	}
	auto edit = protocol::C2SBlockEdit::decode(frame.payload);
	if (!edit) {
		return;
	}
	const replication::EntityState *e = interest_.get(state.net_id);
	core::Vec3d eye = e ? e->pos : core::Vec3d{};
	eye.y += move_params_.eye_height;

	protocol::S2CBlockEditResult result;
	auto per_player =
			replicator_->apply_block_edit(state.net_id, eye, *edit, result);
	send_message(transport_, conn, result);

	for (auto &pf : per_player) {
		for (auto &[other_conn, other] : conns_) {
			if (other.playing && other.net_id == pf.id) {
				send_frames(transport_, other_conn, pf.frames);
				break;
			}
		}
	}
}

const physics::MoveState *ServerSession::player_move_state(core::NetId id) const {
	for (const auto &[conn, state] : conns_) {
		(void)conn;
		if (state.playing && state.net_id == id) {
			return &state.move;
		}
	}
	return nullptr;
}

void ServerSession::tick(double dt_seconds) {
	scratch_.clear();
	transport_.poll(scratch_);

	for (auto &ev : scratch_) {
		switch (ev.kind) {
			case TransportEvent::Kind::kConnected: {
				conns_.try_emplace(ev.conn, ServerHandshake(config_, host_));
				VB_DEBUG("net", "connection ", static_cast<std::uint64_t>(ev.conn),
						" opened");
				break;
			}
			case TransportEvent::Kind::kMessage: {
				auto it = conns_.find(ev.conn);
				if (it == conns_.end()) {
					break;
				}
				std::size_t consumed = 0;
				auto frame = protocol::read_frame(span_of(ev.frame), consumed);
				if (!frame) {
					drop(ev.conn, "malformed frame");
					break;
				}
				if (it->second.playing) {
					if (frame->header.type == protocol::MessageType::kC2SInputBatch) {
						if (auto b = protocol::C2SInputBatch::decode(frame->payload)) {
							handle_input_batch(it->second, *b);
						}
						break;
					}
					if (frame->header.type ==
							protocol::MessageType::kC2SBlockEdit) {
						handle_block_edit(ev.conn, it->second, *frame);
						break;
					}
					// Other post-join C2S messages (chat / UI events) land in
					// later phases; ignore unknown types rather than dropping.
					break;
				}
				auto step = it->second.handshake.on_frame(*frame);
				send_frames(transport_, ev.conn, step.send);
				if (step.completed) {
					it->second.playing = true;
					++playing_;
					const JoinGrant &g = it->second.handshake.grant();
					it->second.net_id = g.net_id;
					it->second.move = physics::MoveState{};
					it->second.move.position = g.spawn_pos;
					interest_.upsert(replication::EntityState{
							g.net_id, core::EntityKindId::kInvalid, g.spawn_pos,
							{}, {} });
					joins_.push_back({ ev.conn, g.net_id, step.player_name });
					VB_INFO("net", "player '", step.player_name, "' joined as net id ",
							static_cast<std::uint32_t>(g.net_id));
				}
				if (step.disconnect) {
					drop(ev.conn, "handshake rejected");
				}
				break;
			}
			case TransportEvent::Kind::kDisconnected: {
				auto it = conns_.find(ev.conn);
				if (it == conns_.end()) {
					break;
				}
				if (it->second.playing) {
					--playing_;
					interest_.remove(it->second.net_id);
					if (replicator_) {
						replicator_->forget_player(it->second.net_id);
					}
					leaves_.push_back({ ev.conn, ev.reason });
				}
				conns_.erase(it);
				break;
			}
		}
	}

	// Handshake timeouts.
	std::vector<ConnId> timed_out;
	for (auto &[conn, state] : conns_) {
		if (state.playing) {
			continue;
		}
		state.age += dt_seconds;
		if (state.age > config_.handshake_timeout_seconds) {
			auto step = state.handshake.on_timeout();
			send_frames(transport_, conn, step.send);
			timed_out.push_back(conn);
		}
	}
	for (ConnId conn : timed_out) {
		drop(conn, "handshake timeout");
	}

	++server_tick_;
	broadcast_snapshots();
	broadcast_world();
}

void ServerSession::broadcast_world() {
	if (!replicator_) {
		return;
	}
	std::vector<std::pair<core::NetId, core::Vec3d>> players;
	for (const auto &[conn, state] : conns_) {
		(void)conn;
		if (!state.playing) {
			continue;
		}
		const replication::EntityState *e = interest_.get(state.net_id);
		players.emplace_back(state.net_id, e ? e->pos : core::Vec3d{});
	}

	for (auto &pf : replicator_->tick(players)) {
		for (auto &[conn, state] : conns_) {
			if (state.playing && state.net_id == pf.id) {
				send_frames(transport_, conn, pf.frames);
				break;
			}
		}
	}
}

namespace {

protocol::EntityRecord to_record(const replication::EntityState &s) {
	protocol::EntityRecord r;
	r.net_id = s.net_id;
	r.kind = s.kind;
	r.pos = s.pos;
	r.rot = s.rot;
	r.vel = s.vel;
	return r;
}

} // namespace

void ServerSession::broadcast_snapshots() {
	for (auto &[conn, state] : conns_) {
		if (!state.playing) {
			continue;
		}
		const replication::EntityState *self = interest_.get(state.net_id);
		const core::Vec3d eye = self ? self->pos : core::Vec3d{};

		std::vector<core::NetId> visible = interest_.visible_from(
				eye, interest_radius_cells_, state.net_id);
		const replication::InterestDiff d =
				replication::diff_interest(state.last_visible, visible);

		protocol::S2CEntitySnapshot snap;
		snap.server_tick = server_tick_;
		for (core::NetId id : d.entered) {
			if (const auto *e = interest_.get(id)) {
				snap.entered.push_back(to_record(*e));
			}
		}
		for (core::NetId id : d.stayed) {
			if (const auto *e = interest_.get(id)) {
				snap.updated.push_back(to_record(*e));
			}
		}
		snap.removed = d.left;

		state.last_visible = std::move(visible);

		snap.last_acked_input_seq = state.last_input_seq;
		if (state.input_driven) {
			snap.has_local = true;
			if (self) {
				snap.local = to_record(*self);
			}
			snap.local.net_id = state.net_id;
			snap.local.pos = state.move.position;
			snap.local.vel = core::Vec3f{
				static_cast<float>(state.move.velocity.x),
				static_cast<float>(state.move.velocity.y),
				static_cast<float>(state.move.velocity.z)
			};
			snap.local.rot = state.look;
			snap.local.flags = pack_flags(state.move.on_ground);
		}

		if (!snap.has_local && snap.entered.empty() && snap.updated.empty() &&
				snap.removed.empty()) {
			continue; // nothing changed for this player this tick
		}
		send_message(transport_, conn, snap);
	}
}

void ServerSession::set_player_state(core::NetId id, core::Vec3d pos,
		core::Vec2f rot, core::Vec3f vel) {
	replication::EntityState s;
	if (const auto *existing = interest_.get(id)) {
		s = *existing;
	}
	s.net_id = id;
	s.pos = pos;
	s.rot = rot;
	s.vel = vel;
	interest_.upsert(s);
}

std::vector<SessionPlayerJoined> ServerSession::take_joins() {
	return std::exchange(joins_, {});
}

std::vector<SessionPlayerLeft> ServerSession::take_leaves() {
	return std::exchange(leaves_, {});
}

// ===========================================================================
// ClientSession
// ===========================================================================

ClientSession::ClientSession(Transport &transport, ConnId conn,
		HandshakeClientConfig config) : transport_(transport),
										conn_(conn),
										handshake_(std::move(config)) {}

void ClientSession::tick(double) {
	scratch_.clear();
	transport_.poll(scratch_);

	for (auto &ev : scratch_) {
		if (ev.conn != conn_) {
			continue;
		}
		switch (ev.kind) {
			case TransportEvent::Kind::kConnected: {
				if (!started_) {
					started_ = true;
					send_frames(transport_, conn_, handshake_.start().send);
				}
				break;
			}
			case TransportEvent::Kind::kMessage: {
				std::size_t consumed = 0;
				auto frame = protocol::read_frame(span_of(ev.frame), consumed);
				if (!frame) {
					failure_reason_ = "malformed frame from server";
					return;
				}
				if (handshake_.status() == ClientHandshakeStatus::kJoined &&
						apply_gameplay_frame(*frame)) {
					break;
				}
				auto step = handshake_.on_frame(*frame);
				send_frames(transport_, conn_, step.send);
				if (step.failed) {
					failure_reason_ = step.failure_reason;
				}
				break;
			}
			case TransportEvent::Kind::kDisconnected: {
				if (handshake_.status() != ClientHandshakeStatus::kJoined &&
						failure_reason_.empty()) {
					failure_reason_ =
							ev.reason.empty() ? "connection closed" : ev.reason;
				}
				break;
			}
		}
	}
}

bool ClientSession::apply_gameplay_frame(const protocol::Frame &frame) {
	using protocol::MessageType;
	switch (frame.header.type) {
		case MessageType::kS2CEntitySnapshot: {
			if (auto snap = protocol::S2CEntitySnapshot::decode(frame.payload)) {
				apply_snapshot(*snap);
			}
			return true;
		}
		case MessageType::kS2CChunkAdd: {
			if (auto m = protocol::S2CChunkAdd::decode(frame.payload)) {
				(void)chunks_.apply_add(*m);
			}
			return true;
		}
		case MessageType::kS2CChunkDelta: {
			if (auto m = protocol::S2CChunkDelta::decode(frame.payload)) {
				(void)chunks_.apply_delta(*m);
				forget_pending_edits_for(m->coord); // authoritative wins
			}
			return true;
		}
		case MessageType::kS2CBlockEditResult: {
			if (auto m = protocol::S2CBlockEditResult::decode(frame.payload)) {
				handle_block_edit_result(*m);
			}
			return true;
		}
		case MessageType::kS2CChunkRemove: {
			if (auto m = protocol::S2CChunkRemove::decode(frame.payload)) {
				chunks_.apply_remove(*m);
			}
			return true;
		}
		default:
			return false;
	}
}

void ClientSession::apply_snapshot(const protocol::S2CEntitySnapshot &snap) {
	last_server_tick_ = snap.server_tick;

	const auto ingest = [&](const protocol::EntityRecord &r) {
		remote_[r.net_id] = r;
		RemoteSample &s = remote_samples_[r.net_id];
		if (s.cur_tick == 0) {
			s.prev_pos = r.pos;
			s.prev_tick = snap.server_tick;
		} else if (s.cur_tick != snap.server_tick) {
			s.prev_pos = s.cur_pos;
			s.prev_tick = s.cur_tick;
		}
		s.cur_pos = r.pos;
		s.cur_tick = snap.server_tick;
	};
	for (const auto &r : snap.entered) {
		ingest(r);
	}
	for (const auto &r : snap.updated) {
		ingest(r);
	}
	for (core::NetId id : snap.removed) {
		remote_.erase(id);
		remote_samples_.erase(id);
	}

	if (snap.has_local) {
		reconcile(snap.local, snap.last_acked_input_seq);
	}
}

void ClientSession::reconcile(const protocol::EntityRecord &authoritative,
		std::uint32_t acked_seq) {
	last_acked_seq_ = acked_seq;
	predicted_.position = authoritative.pos;
	predicted_.velocity = core::Vec3d{ static_cast<double>(authoritative.vel.x),
		static_cast<double>(authoritative.vel.y),
		static_cast<double>(authoritative.vel.z) };
	predicted_.on_ground = (authoritative.flags & 1u) != 0;

	history_.erase(std::remove_if(history_.begin(), history_.end(),
						   [acked_seq](const protocol::InputCmd &c) {
							   return c.seq <= acked_seq;
						   }),
			history_.end());

	for (const auto &c : history_) {
		predicted_ = physics::step_movement(predicted_, to_move_input(c),
				move_params_, chunks_);
	}
}

void ClientSession::push_input(const protocol::InputCmd &cmd) {
	if (!joined()) {
		return;
	}
	predicted_ = physics::step_movement(predicted_, to_move_input(cmd),
			move_params_, chunks_);
	history_.push_back(cmd);
	while (history_.size() > protocol::C2SInputBatch::kMaxCmds) {
		history_.erase(history_.begin());
	}
	protocol::C2SInputBatch batch;
	batch.cmds = history_;
	send_message(transport_, conn_, batch);
}

void ClientSession::push_block_edit(const protocol::C2SBlockEdit &edit) {
	if (!joined()) {
		return;
	}
	const core::BlockId applied = edit.action == protocol::BlockEditAction::kBreak
			? core::BlockId::kAir
			: edit.block;
	const core::BlockId prev = chunks_.edit_block(edit.pos, applied);
	pending_edits_.push_back(
			{ edit.predicted_seq, edit.pos, prev, core::chunk_of(edit.pos) });
	send_message(transport_, conn_, edit);
}

void ClientSession::handle_block_edit_result(
		const protocol::S2CBlockEditResult &res) {
	for (auto it = pending_edits_.begin(); it != pending_edits_.end(); ++it) {
		if (it->seq != res.predicted_seq) {
			continue;
		}
		if (!res.accepted) {
			// Roll the optimistic apply back; the authoritative delta (if any)
			// will still correct light on accept.
			chunks_.edit_block(it->pos, it->prev);
		}
		pending_edits_.erase(it);
		return;
	}
}

void ClientSession::forget_pending_edits_for(core::ChunkCoord coord) {
	std::erase_if(pending_edits_,
			[coord](const PendingEdit &e) { return e.coord == coord; });
}

core::Vec3d ClientSession::interpolated_pos(core::NetId id) const {
	const auto it = remote_samples_.find(id);
	if (it == remote_samples_.end()) {
		const auto r = remote_.find(id);
		return r == remote_.end() ? core::Vec3d{} : r->second.pos;
	}
	const RemoteSample &s = it->second;
	if (s.cur_tick <= s.prev_tick) {
		return s.cur_pos;
	}
	// Render ~1 tick behind the newest sample (spec §8.4 interpolation delay).
	const double span = static_cast<double>(s.cur_tick - s.prev_tick);
	const double target =
			static_cast<double>(last_server_tick_) - 1.0 - static_cast<double>(s.prev_tick);
	const double a = core::clamp(span > 0.0 ? target / span : 1.0, 0.0, 1.0);
	return s.prev_pos + (s.cur_pos - s.prev_pos) * a;
}

} // namespace vb::net
