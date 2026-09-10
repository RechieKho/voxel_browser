#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "vb/core/error.hpp"
#include "vb/core/result.hpp"
#include "vb/protocol/message.hpp"

// Transport abstraction (spec §8.1). The application speaks in whole framed
// messages on one of five lanes; the backend preserves message boundaries and
// applies the lane's reliability. Two backends:
//
//   LoopbackTransport  in-process, deterministic, no dependencies — used by
//                      unit/integration tests and the integrated singleplayer
//                      server.
//   GnsTransport       GameNetworkingSockets over UDP — built only when
//                      VB_WITH_NET is on (Phase 1.2 spike).
//
// Poll once per tick; drain every event before advancing simulation.

namespace vb::net {

enum class ConnId : std::uint64_t { kInvalid = 0 };

enum class SendMode : std::uint8_t {
	kReliableOrdered,
	kUnreliable,
};

// Reliability per lane (spec §8.2).
constexpr SendMode send_mode_for_lane(protocol::Lane lane) {
	switch (lane) {
		case protocol::Lane::kControl:
		case protocol::Lane::kWorld:
		case protocol::Lane::kAssets:
			return SendMode::kReliableOrdered;
		case protocol::Lane::kSnapshot:
		case protocol::Lane::kInput:
			return SendMode::kUnreliable;
	}
	return SendMode::kReliableOrdered;
}

struct TransportEvent {
	enum class Kind : std::uint8_t {
		kConnected, // a peer connection is established (server: inbound)
		kDisconnected, // connection closed (locally or remotely)
		kMessage, // one complete framed message arrived
	};

	Kind kind = Kind::kConnected;
	ConnId conn = ConnId::kInvalid;

	// kMessage only: a complete envelope + payload (parse with read_frame()).
	protocol::Lane lane = protocol::Lane::kControl;
	std::vector<std::byte> frame;

	// kDisconnected only.
	std::string reason;
};

class Transport {
public:
	virtual ~Transport() = default;

	// Server: begin accepting connections. Client transports never call this.
	virtual core::Status<core::NetError> listen(std::uint16_t port) = 0;

	// Client: open a connection. The ConnId is usable immediately; a
	// kConnected event follows once the link is up.
	virtual core::Result<ConnId, core::NetError> connect(
			std::string_view host, std::uint16_t port) = 0;

	// Send one complete framed message. `frame` must be a full envelope+payload
	// (see protocol::write_frame). Silently dropped if `conn` is unknown.
	virtual void send(ConnId conn, protocol::Lane lane,
			std::span<const std::byte> frame) = 0;

	// Begin closing a connection; a kDisconnected event will follow.
	virtual void close(ConnId conn, std::string_view reason) = 0;

	// Drain all pending events into `out` (appended, not cleared for you here —
	// callers typically clear first).
	virtual void poll(std::vector<TransportEvent> &out) = 0;

	virtual bool is_server() const = 0;
	virtual std::size_t connection_count() const = 0;
};

// Convenience: frame a typed message and send it on its natural lane.
template <typename Msg>
void send_message(Transport &t, ConnId conn, const Msg &msg,
		std::uint16_t flags = 0) {
	std::vector<std::byte> payload;
	msg.encode(payload);
	std::vector<std::byte> frame;
	protocol::write_frame(frame, Msg::kType, payload, flags);
	t.send(conn, protocol::lane_for(Msg::kType), frame);
}

} // namespace vb::net
