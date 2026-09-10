#include "vb/net/integrated.hpp"

#include <utility>

#include "vb/core/log.hpp"

namespace vb::net {

IntegratedGame::IntegratedGame(HandshakeServerConfig server_config,
		HandshakeClientConfig client_config, HandshakeServerHost server_host) : server_(net_.server(), std::move(server_config), std::move(server_host)) {
	auto listening = net_.server().listen(0);
	(void)listening; // loopback listen never fails on a fresh network

	Transport &client_transport = net_.create_client();
	auto conn = client_transport.connect("integrated", 0);
	if (!conn) {
		VB_ERROR("net", "integrated client failed to connect to loopback server");
		return;
	}
	client_.emplace(client_transport, *conn, std::move(client_config));
}

void IntegratedGame::tick(double dt_seconds) {
	// Server first so the client sees this step's responses next tick; two
	// tick() calls per exchanged message pair, which is fine at 20-60 Hz.
	server_.tick(dt_seconds);
	if (client_) {
		client_->tick(dt_seconds);
	}
}

} // namespace vb::net
