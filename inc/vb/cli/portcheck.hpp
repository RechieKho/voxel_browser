#pragma once

#include <cstdint>

namespace vb::cli {

// Best-effort pre-launch check: can a UDP socket bind 0.0.0.0:`port` right
// now? (The server's own bind is the authority; this just turns the common
// "another server already uses that port" into a clear error before a detached
// process dies unseen.) Port 0 means "any free port" and is always available.
bool udp_port_available(std::uint16_t port);

} // namespace vb::cli
