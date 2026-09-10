#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "vb/core/ids.hpp"
#include "vb/net/handshake.hpp"
#include "vb/net/transport.hpp"
#include "vb/protocol/handshake.hpp"

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

private:
	struct Conn {
		ServerHandshake handshake;
		double age = 0.0;
		bool playing = false;
	};

	void drop(ConnId conn, const std::string &reason);

	Transport &transport_;
	HandshakeServerConfig config_;
	HandshakeServerHost host_;
	std::map<ConnId, Conn> conns_;
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

private:
	Transport &transport_;
	ConnId conn_;
	ClientHandshake handshake_;
	bool started_ = false;
	std::string failure_reason_;
	std::vector<TransportEvent> scratch_;
};

} // namespace vb::net
