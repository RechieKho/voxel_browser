#pragma once

#include <cstdint>
#include <memory>

#include "vb/net/transport.hpp"

// Transport backend over GameNetworkingSockets — real UDP (spec §8.1), built
// only when VB_WITH_NET is on. Without it, every call fails with
// core::NetError::kBackendUnavailable, so callers (ServerSession/ClientSession,
// server/client main.cpp) never need an #ifdef at the use site — same pattern
// as vb::script::Vm for VB_WITH_LUA.
//
// One GnsTransport per logical endpoint: the server owns one (listen()); each
// client connection owns one (connect()). All GnsTransport instances in a
// process share GameNetworkingSockets' single global interface + connection-
// status callback internally (see gns_transport.cpp) — this only matters for
// tests that run a server and several clients in one process; application
// code just uses the Transport interface normally.
//
// Threading: like LoopbackTransport, a GnsTransport must only ever be
// poll()ed/used from one thread. GNS callbacks fire synchronously from inside
// poll()'s RunCallbacks() call, never from a background thread.

namespace vb::net {

class GnsTransport final : public Transport {
public:
	GnsTransport();
	~GnsTransport() override;

	GnsTransport(const GnsTransport &) = delete;
	GnsTransport &operator=(const GnsTransport &) = delete;

	core::Status<core::NetError> listen(std::uint16_t port) override;
	core::Result<ConnId, core::NetError> connect(std::string_view host,
			std::uint16_t port) override;
	void send(ConnId conn, protocol::Lane lane,
			std::span<const std::byte> frame) override;
	void close(ConnId conn, std::string_view reason) override;
	void poll(std::vector<TransportEvent> &out) override;
	bool is_server() const override;
	std::size_t connection_count() const override;

	// The actual bound UDP port after a successful listen(); differs from the
	// requested port when 0 ("any free port") was passed. 0 if not listening.
	std::uint16_t bound_port() const;

	struct Impl;

private:
	std::unique_ptr<Impl> impl_;
};

} // namespace vb::net
