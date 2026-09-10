#include "vb/net/session.hpp"

#include <span>
#include <utility>

#include "vb/core/log.hpp"
#include "vb/protocol/message.hpp"
#include "vb/protocol/world.hpp"

namespace vb::net {

namespace {

std::span<const std::byte> span_of(const std::vector<std::byte> &v) {
	return { v.data(), v.size() };
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
				auto step = it->second.handshake.on_frame(*frame);
				send_frames(transport_, ev.conn, step.send);
				if (step.completed) {
					it->second.playing = true;
					++playing_;
					const JoinGrant &g = it->second.handshake.grant();
					it->second.net_id = g.net_id;
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

		if (snap.entered.empty() && snap.updated.empty() && snap.removed.empty()) {
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
	for (const auto &r : snap.entered) {
		remote_[r.net_id] = r;
	}
	for (const auto &r : snap.updated) {
		remote_[r.net_id] = r;
	}
	for (core::NetId id : snap.removed) {
		remote_.erase(id);
	}
}

} // namespace vb::net
