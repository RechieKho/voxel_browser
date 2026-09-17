#include "vb/net/session.hpp"

#include <algorithm>
#include <span>
#include <utility>

#include "vb/core/log.hpp"
#include "vb/core/math.hpp"
#include "vb/protocol/message.hpp"
#include "vb/protocol/world.hpp"
#include "vb/world/daynight.hpp"

namespace vb::net {

namespace {

std::span<const std::byte> span_of(const std::vector<std::byte> &v) {
	return { v.data(), v.size() };
}

// Per-tick S2C_AssetData send budget (spec §9.3's pacing, simple per-tick
// cap rather than literal byte-in-flight windowing).
constexpr int kAssetSendBudgetPerTick = 4;

// How often S2C_TimeOfDay goes out to already-connected clients (spec §5.4).
// Coarser than snapshots/world state -- the clock only needs to look smooth,
// not be exact every tick.
constexpr double kTimeOfDayBroadcastIntervalSeconds = 1.0;

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
		grant.time_of_day = static_cast<std::uint32_t>(time_of_day_ticks_);
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
	auto &pos = registry_.get<ecs::Position>(conn.entity);
	auto &vel = registry_.get<ecs::Velocity>(conn.entity);
	auto &rot = registry_.get<ecs::Rotation>(conn.entity);
	auto &collider = registry_.get<ecs::Collider>(conn.entity);
	auto &input = registry_.get<ecs::PlayerInput>(conn.entity);

	physics::MoveState move{ pos.value, vel.value, collider.on_ground };
	for (const auto &cmd : batch.cmds) {
		if (cmd.seq <= input.last_seq) {
			continue; // already simulated (batches resend recent commands)
		}
		// The spawn chunk may still be generating (async worldgen worker) --
		// simulating gravity against unloaded-as-air terrain lets the player
		// free-fall with no collision and end up embedded in the ground the
		// moment it finishes loading. Freeze position/velocity until the
		// column is actually there; still ack the seq so the client doesn't
		// pile up a backlog to replay once it unfreezes.
		if (replicator_ == nullptr ||
				physics::ground_area_loaded(move.position,
						[this](core::ChunkCoord c) {
							return replicator_->world().has_chunk(c);
						})) {
			move = physics::step_movement(move, to_move_input(cmd),
					move_params_, world);
		}
		input.last_seq = cmd.seq;
		rot.yaw = cmd.yaw;
		rot.pitch = cmd.pitch;
	}
	pos.value = move.position;
	vel.value = move.velocity;
	collider.on_ground = move.on_ground;
	conn.input_driven = true;

	replication::EntityState s;
	if (const auto *e = interest_.get(conn.net_id)) {
		s = *e;
	}
	s.net_id = conn.net_id;
	s.pos = pos.value;
	s.rot = { rot.yaw, rot.pitch };
	s.vel = core::Vec3f{ static_cast<float>(vel.value.x),
		static_cast<float>(vel.value.y),
		static_cast<float>(vel.value.z) };
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

void ServerSession::handle_chat(Conn &state, const protocol::Frame &frame) {
	auto msg = protocol::C2SChat::decode(frame.payload);
	if (!msg) {
		return;
	}
	if (msg->text.empty()) {
		return;
	}
	if (on_chat_ && !on_chat_(state.net_id, msg->text)) {
		return; // vetoed by the pack (vb.on("chat"))
	}
	const protocol::S2CChat out{
		registry_.get<ecs::PlayerTag>(state.entity).name + ": " + msg->text
	};
	for (auto &[other_conn, other] : conns_) {
		if (other.playing) {
			send_message(transport_, other_conn, out);
		}
	}
}

std::optional<physics::MoveState> ServerSession::player_move_state(core::NetId id) const {
	for (const auto &[conn, state] : conns_) {
		(void)conn;
		if (state.playing && state.net_id == id) {
			return physics::MoveState{
				registry_.get<ecs::Position>(state.entity).value,
				registry_.get<ecs::Velocity>(state.entity).value,
				registry_.get<ecs::Collider>(state.entity).on_ground
			};
		}
	}
	return std::nullopt;
}

void ServerSession::set_player_velocity(core::NetId id, core::Vec3d vel) {
	for (auto &[conn, state] : conns_) {
		(void)conn;
		if (state.playing && state.net_id == id) {
			registry_.get<ecs::Velocity>(state.entity).value = vel;
			return;
		}
	}
}

ConnId ServerSession::conn_for_player(core::NetId id) const {
	for (const auto &[conn, state] : conns_) {
		if (state.playing && state.net_id == id) {
			return conn;
		}
	}
	return ConnId::kInvalid;
}

std::string_view ServerSession::player_name(core::NetId id) const {
	for (const auto &[conn, state] : conns_) {
		(void)conn;
		if (state.playing && state.net_id == id) {
			return registry_.get<ecs::PlayerTag>(state.entity).name;
		}
	}
	return {};
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
					if (frame->header.type == protocol::MessageType::kC2SUiEvent) {
						if (auto e = protocol::C2SUiEvent::decode(frame->payload)) {
							if (on_ui_event_) {
								on_ui_event_(it->second.net_id, *e);
							}
						}
						break;
					}
					if (frame->header.type == protocol::MessageType::kC2SChat) {
						handle_chat(it->second, *frame);
						break;
					}
					// Other post-join C2S messages land in later phases;
					// ignore unknown types rather than dropping.
					break;
				}
				auto step = it->second.handshake.on_frame(*frame);
				send_frames(transport_, ev.conn, step.send);
				if (step.completed) {
					it->second.playing = true;
					++playing_;
					const JoinGrant &g = it->second.handshake.grant();
					it->second.net_id = g.net_id;
					it->second.spawn_pos = g.spawn_pos;
					it->second.entity = registry_.create();
					registry_.emplace<ecs::Position>(it->second.entity, g.spawn_pos);
					registry_.emplace<ecs::Velocity>(it->second.entity);
					registry_.emplace<ecs::Rotation>(it->second.entity);
					registry_.emplace<ecs::Collider>(
							it->second.entity, move_params_, /*on_ground=*/false);
					registry_.emplace<ecs::PlayerInput>(it->second.entity);
					registry_.emplace<ecs::Health>(it->second.entity, 20.0f, 20.0f);
					registry_.emplace<ecs::PlayerTag>(it->second.entity, step.player_name);
					registry_.emplace<ecs::NetReplicated>(it->second.entity, g.net_id);
					interest_.upsert(replication::EntityState{
							g.net_id, core::EntityKindId::kInvalid, g.spawn_pos,
							{}, {} });
					joins_.push_back({ ev.conn, g.net_id, step.player_name });
					VB_INFO("net", "player '", step.player_name, "' joined as net id ",
							static_cast<std::uint32_t>(g.net_id));

					// Phase 5.4: tell the newcomer who's already here, and
					// tell everyone already here that they joined.
					protocol::S2CPlayerList list_msg;
					for (auto &[other_conn, other] : conns_) {
						if (other.playing && other_conn != ev.conn) {
							list_msg.players.push_back({ other.net_id,
									registry_.get<ecs::PlayerTag>(other.entity).name });
						}
					}
					send_message(transport_, ev.conn, list_msg);
					const protocol::S2CPlayerJoin join_msg{ g.net_id,
						step.player_name };
					for (auto &[other_conn, other] : conns_) {
						if (other.playing && other_conn != ev.conn) {
							send_message(transport_, other_conn, join_msg);
						}
					}
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
					leaves_.push_back({ ev.conn, it->second.net_id, ev.reason });
					const protocol::S2CPlayerLeave leave_msg{ it->second.net_id };
					for (auto &[other_conn, other] : conns_) {
						if (other_conn != ev.conn && other.playing) {
							send_message(transport_, other_conn, leave_msg);
						}
					}
					registry_.destroy(it->second.entity);
				}
				conns_.erase(it);
				break;
			}
		}
	}

	// Handshake timeouts + asset-stream pacing.
	std::vector<std::pair<ConnId, std::string>> to_drop;
	for (auto &[conn, state] : conns_) {
		if (state.playing) {
			continue;
		}
		if (state.handshake.state() == ServerHandshakeState::kStreamingAssets) {
			auto step = state.handshake.pump_assets(kAssetSendBudgetPerTick);
			send_frames(transport_, conn, step.send);
			if (step.disconnect) {
				to_drop.emplace_back(conn, "asset streaming protocol error");
				continue;
			}
		}
		state.age += dt_seconds;
		if (state.age > config_.handshake_timeout_seconds) {
			auto step = state.handshake.on_timeout();
			send_frames(transport_, conn, step.send);
			to_drop.emplace_back(conn, "handshake timeout");
		}
	}
	for (const auto &[conn, reason] : to_drop) {
		drop(conn, reason);
	}

	time_of_day_ticks_ = world::advance_time_of_day(
			time_of_day_ticks_, dt_seconds, day_length_seconds_);
	time_of_day_broadcast_accum_ += dt_seconds;
	if (time_of_day_broadcast_accum_ >= kTimeOfDayBroadcastIntervalSeconds) {
		time_of_day_broadcast_accum_ = 0.0;
		broadcast_time_of_day();
	}

	check_respawns();
	update_item_drops(dt_seconds);

	++server_tick_;
	broadcast_snapshots();
	broadcast_world();
}

void ServerSession::update_item_drops(double dt_seconds) {
	// Reads positions from the interest grid rather than Conn::move.position
	// directly: it's the same single source of truth broadcast_snapshots()
	// already uses for "where is this net id right now", kept current by
	// both real input-driven movement (handle_input_batch) and the
	// test/script-facing set_player_state() -- picking up an item works the
	// same way regardless of which path moved the player.
	std::vector<std::pair<core::NetId, core::Vec3d>> players;
	for (auto &[conn, state] : conns_) {
		if (!state.playing) {
			continue;
		}
		if (const auto *e = interest_.get(state.net_id)) {
			players.emplace_back(state.net_id, e->pos);
		}
	}
	const world::ItemDropTickResult result = item_drops_.tick(dt_seconds, players);
	for (core::NetId id : result.removed) {
		interest_.remove(id);
	}
	for (const world::ItemPickup &p : result.pickups) {
		if (on_item_pickup_) {
			on_item_pickup_(p.player, p.item, p.count);
		}
	}
}

void ServerSession::apply_damage(Conn &state, float amount, std::string_view cause) {
	auto &health = registry_.get<ecs::Health>(state.entity);
	if (health.current <= 0.0f) {
		return; // already at 0, awaiting this tick's respawn
	}
	const float before = health.current;
	health.current = std::max(0.0f, health.current - amount);
	if (health.current <= 0.0f) {
		state.death_cause = std::string(cause);
		state.death_health_before = before;
	}
}

void ServerSession::damage_player(core::NetId id, float amount, std::string_view cause) {
	for (auto &[conn, state] : conns_) {
		(void)conn;
		if (state.playing && state.net_id == id) {
			apply_damage(state, amount, cause);
			return;
		}
	}
}

core::Vec3d ServerSession::spawn_point(core::NetId id) const {
	for (const auto &[conn, state] : conns_) {
		(void)conn;
		if (state.playing && state.net_id == id) {
			return state.spawn_pos;
		}
	}
	return {};
}

void ServerSession::check_respawns() {
	for (auto &[conn, state] : conns_) {
		if (!state.playing) {
			continue;
		}
		auto &pos = registry_.get<ecs::Position>(state.entity);
		auto &health = registry_.get<ecs::Health>(state.entity);
		if (pos.value.y < void_kill_y_ && health.current > 0.0f) {
			apply_damage(state, health.current, "void");
		}
		if (health.current > 0.0f) {
			continue;
		}
		RespawnDecision decision{ health.max, state.spawn_pos,
			"* you died and respawned" };
		if (on_respawn_) {
			decision = on_respawn_(
					state.net_id, state.death_cause, state.death_health_before);
		}
		health.current = decision.heal_to;
		pos.value = decision.pos;
		registry_.get<ecs::Velocity>(state.entity).value = {};
		registry_.get<ecs::Collider>(state.entity).on_ground = false;
		replication::EntityState s;
		if (const auto *e = interest_.get(state.net_id)) {
			s = *e;
		}
		s.net_id = state.net_id;
		s.pos = decision.pos;
		s.vel = {};
		interest_.upsert(s);
		if (!decision.message.empty()) {
			send_message(transport_, conn, protocol::S2CChat{ decision.message });
		}
		state.death_cause.clear();
		state.death_health_before = 0.0f;
	}
}

core::NetId ServerSession::spawn_item_drop(
		core::Vec3d pos, core::BlockId item, std::uint16_t count) {
	const core::NetId id = item_drops_.spawn(pos, item, count);
	interest_.upsert(replication::EntityState{
			id, world::kItemDropKind, pos, {}, {} });
	return id;
}

void ServerSession::broadcast_time_of_day() {
	const protocol::S2CTimeOfDay msg{ static_cast<std::uint32_t>(time_of_day_ticks_) };
	for (auto &[conn, state] : conns_) {
		if (state.playing) {
			send_message(transport_, conn, msg);
		}
	}
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

		snap.last_acked_input_seq = registry_.get<ecs::PlayerInput>(state.entity).last_seq;
		if (state.input_driven) {
			snap.has_local = true;
			if (self) {
				snap.local = to_record(*self);
			}
			const auto &pos = registry_.get<ecs::Position>(state.entity);
			const auto &vel = registry_.get<ecs::Velocity>(state.entity);
			const auto &rot = registry_.get<ecs::Rotation>(state.entity);
			const auto &collider = registry_.get<ecs::Collider>(state.entity);
			snap.local.net_id = state.net_id;
			snap.local.pos = pos.value;
			snap.local.vel = core::Vec3f{
				static_cast<float>(vel.value.x),
				static_cast<float>(vel.value.y),
				static_cast<float>(vel.value.z)
			};
			snap.local.rot = { rot.yaw, rot.pitch };
			snap.local.flags = pack_flags(collider.on_ground);
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

namespace {

HandshakeClientHost make_asset_host(assetsync::ClientAssetCache *cache) {
	if (cache == nullptr) {
		return {};
	}
	HandshakeClientHost host;
	host.assets_missing = [cache](const std::vector<protocol::AssetEntryRecord> &entries) {
		return cache->compute_missing(entries);
	};
	host.on_asset_chunk = [cache](const protocol::S2CAssetData &chunk) {
		return cache->ingest_chunk(chunk);
	};
	host.assets_all_received = [cache] { return cache->all_received(); };
	return host;
}

} // namespace

ClientSession::ClientSession(Transport &transport, ConnId conn,
		HandshakeClientConfig config, assetsync::ClientAssetCache *cache)
		: transport_(transport), conn_(conn),
		  handshake_(std::move(config), make_asset_host(cache)),
		  asset_cache_(cache) {}

const std::unordered_map<std::string, std::vector<std::byte>> &
ClientSession::virtual_pack_fs() const {
	static const std::unordered_map<std::string, std::vector<std::byte>> kEmpty;
	return asset_cache_ != nullptr ? asset_cache_->virtual_fs() : kEmpty;
}

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
				// Handled unconditionally (not through the handshake FSM's
				// strict per-state type checks nor gated on kJoined): on a
				// real transport this travels on a different lane than
				// JoinAccept with no cross-lane ordering guarantee, so it may
				// arrive just before or just after it.
				if (frame->header.type == protocol::MessageType::kS2CBlockRegistry) {
					if (auto m = protocol::S2CBlockRegistry::decode(frame->payload)) {
						apply_block_registry(*m);
					} else {
						VB_ERROR("net", "malformed S2C_BlockRegistry: ",
								core::message(m.error()));
					}
					break;
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
				// A failure here used to be silently swallowed: the chunk would
				// never render (mesher sees it as unloaded) yet the server still
				// thinks it was sent (last_sent_ includes it), so it's never
				// retried -- a permanent, invisible hole with no trace of why.
				// Log it loudly so a report like that has something to go on.
				if (auto applied = chunks_.apply_add(*m); !applied) {
					VB_ERROR("net", "chunk (", m->coord.x, ",", m->coord.y, ",",
							m->coord.z, ") add rejected: ",
							core::message(applied.error()));
				}
			} else {
				VB_ERROR("net", "malformed S2C_ChunkAdd: ", core::message(m.error()));
			}
			return true;
		}
		case MessageType::kS2CChunkDelta: {
			if (auto m = protocol::S2CChunkDelta::decode(frame.payload)) {
				if (auto applied = chunks_.apply_delta(*m); !applied) {
					VB_ERROR("net", "chunk (", m->coord.x, ",", m->coord.y, ",",
							m->coord.z, ") delta rejected: ",
							core::message(applied.error()));
				}
				forget_pending_edits_for(m->coord); // authoritative wins
			} else {
				VB_ERROR("net", "malformed S2C_ChunkDelta: ", core::message(m.error()));
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
		case MessageType::kS2COpenUi: {
			if (auto m = protocol::S2COpenUi::decode(frame.payload)) {
				pending_open_ui_ = std::move(*m);
			} else {
				VB_ERROR("net", "malformed S2C_OpenUi: ", core::message(m.error()));
			}
			return true;
		}
		case MessageType::kS2CChat: {
			if (auto m = protocol::S2CChat::decode(frame.payload)) {
				pending_chat_.push_back(std::move(m->text));
			} else {
				VB_ERROR("net", "malformed S2C_Chat: ", core::message(m.error()));
			}
			return true;
		}
		case MessageType::kS2CTimeOfDay: {
			if (auto m = protocol::S2CTimeOfDay::decode(frame.payload)) {
				time_of_day_override_ = m->time_of_day;
			} else {
				VB_ERROR("net", "malformed S2C_TimeOfDay: ",
						core::message(m.error()));
			}
			return true;
		}
		case MessageType::kS2CPlayerList: {
			if (auto m = protocol::S2CPlayerList::decode(frame.payload)) {
				players_.clear();
				for (auto &p : m->players) {
					players_[p.net_id] = std::move(p.name);
				}
			} else {
				VB_ERROR("net", "malformed S2C_PlayerList: ",
						core::message(m.error()));
			}
			return true;
		}
		case MessageType::kS2CPlayerJoin: {
			if (auto m = protocol::S2CPlayerJoin::decode(frame.payload)) {
				pending_chat_.push_back("* " + m->name + " joined the game");
				players_[m->net_id] = std::move(m->name);
			} else {
				VB_ERROR("net", "malformed S2C_PlayerJoin: ",
						core::message(m.error()));
			}
			return true;
		}
		case MessageType::kS2CInventory: {
			if (auto m = protocol::S2CInventory::decode(frame.payload)) {
				inventory_ = std::move(m->slots);
			} else {
				VB_ERROR("net", "malformed S2C_Inventory: ",
						core::message(m.error()));
			}
			return true;
		}
		case MessageType::kS2CPlayerLeave: {
			if (auto m = protocol::S2CPlayerLeave::decode(frame.payload)) {
				auto it = players_.find(m->net_id);
				const std::string name = it != players_.end() ? it->second : "player";
				if (it != players_.end()) {
					players_.erase(it);
				}
				pending_chat_.push_back("* " + name + " left the game");
			} else {
				VB_ERROR("net", "malformed S2C_PlayerLeave: ",
						core::message(m.error()));
			}
			return true;
		}
		default:
			return false;
	}
}

std::optional<protocol::S2COpenUi> ClientSession::take_open_ui() {
	std::optional<protocol::S2COpenUi> out = std::move(pending_open_ui_);
	pending_open_ui_.reset();
	return out;
}

std::vector<std::string> ClientSession::take_chat_messages() {
	std::vector<std::string> out = std::move(pending_chat_);
	pending_chat_.clear();
	return out;
}

void ClientSession::apply_block_registry(const protocol::S2CBlockRegistry &msg) {
	world::BlockRegistry reg;
	for (const auto &b : msg.blocks) {
		reg.add({ b.name, b.solid, b.opaque, b.liquid, b.light_emission });
	}
	VB_INFO("net", "received block registry (", msg.blocks.size(), " blocks)");
	chunks_.set_registry(std::move(reg));
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
	// Mirror the server's freeze while the local chunk mirror doesn't have the
	// spawn column yet -- otherwise the client predicts its own independent
	// fall into empty space and gets snapped back once a reconcile catches up,
	// which looks like falling through the world even when the server itself
	// never actually let the player move (see ServerSession::handle_input_batch).
	if (physics::ground_area_loaded(predicted_.position,
				[this](core::ChunkCoord c) { return chunks_.has(c); })) {
		predicted_ = physics::step_movement(predicted_, to_move_input(cmd),
				move_params_, chunks_);
	}
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
