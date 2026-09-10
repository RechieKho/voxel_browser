# vb/net — transport abstraction + connection handshake. Compiled into vb_core.
# The GnsTransport backend (GameNetworkingSockets) is added here under
# VB_WITH_NET once the Phase 1.2 spike confirms the build.

target_sources(vb_core PRIVATE
  loopback.cpp
  handshake.cpp
)
