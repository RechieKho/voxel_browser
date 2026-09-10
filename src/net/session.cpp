#include "vb/net/session.hpp"

#include <span>
#include <utility>

#include "vb/core/log.hpp"
#include "vb/protocol/message.hpp"

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
				conns_.emplace(ev.conn, Conn{ ServerHandshake(config_, host_) });
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

} // namespace vb::net
