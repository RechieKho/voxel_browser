#pragma once

#include <memory>

#include "vb/auth/config.hpp"
#include "vb/auth/http.hpp"
#include "vb/auth/service.hpp"
#include "vb/net/handshake.hpp"

// Wires an active AuthConfig into a server's handshake (auth.md §5.1/§5.2).
// Shared by the dedicated server and the singleplayer integrated server so both
// run the identical code path: no fake "local" login anywhere.

namespace vb::auth {

// auth_mode = kExternal plus the re-auth schedule (derived from auth.lua's
// presence, never configured).
void apply_external_auth(net::HandshakeServerConfig &config, const AuthConfig &auth);

// Starts an AuthService (background key prefetch) and sets
// host.auth_challenge / host.begin_authenticate. The returned service must be
// kept alive as long as the host is in use (the host also holds a reference).
// Name collisions and the pack's join veto run later, in the handshake FSM.
std::shared_ptr<AuthService> install_external_auth(net::HandshakeServerHost &host,
		const AuthConfig &auth, std::shared_ptr<HttpFetcher> http);

} // namespace vb::auth
