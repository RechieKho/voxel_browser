#pragma once

#include <cstdint>
#include <deque>
#include <memory>
#include <vector>

#include "vb/net/transport.hpp"

// In-process Transport backend. Deterministic and dependency-free: messages are
// delivered on the next poll() of the peer, in order, with no loss. Used for
// unit/integration tests and (later) the integrated singleplayer server, which
// runs a real server object over a Loopback link instead of a UDP socket.

namespace vb::net {

class LoopbackNetwork {
public:
	struct Hub; // opaque; defined in loopback.cpp

	LoopbackNetwork();

	// The single server-side transport.
	Transport &server();

	// Create a new client transport. Owned by this LoopbackNetwork; valid until
	// the LoopbackNetwork is destroyed.
	Transport &create_client();

private:
	std::shared_ptr<Hub> hub_;
	std::unique_ptr<Transport> server_;
	std::vector<std::unique_ptr<Transport>> clients_;
};

} // namespace vb::net
