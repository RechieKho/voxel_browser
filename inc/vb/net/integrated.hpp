#pragma once

#include <optional>

#include "vb/net/loopback.hpp"
#include "vb/net/session.hpp"

// Integrated singleplayer: a real ServerSession and ClientSession wired together
// over an in-process LoopbackNetwork (spec §3 — "no separate singleplayer code
// path"). The client binary uses this for --singleplayer; integration tests use
// it to exercise the full session stack without sockets.

namespace vb::net {

class IntegratedGame {
public:
	IntegratedGame(HandshakeServerConfig server_config,
			HandshakeClientConfig client_config,
			HandshakeServerHost server_host = {});

	// Advance both ends by one step.
	void tick(double dt_seconds);

	ServerSession &server() { return server_; }
	ClientSession &client() { return *client_; }

	bool client_joined() const { return client_ && client_->joined(); }
	bool client_failed() const { return client_ && client_->failed(); }

private:
	LoopbackNetwork net_;
	ServerSession server_;
	std::optional<ClientSession> client_;
};

} // namespace vb::net
