# vb/net — transport abstraction + connection handshake. Compiled into vb_core.
# gns_transport.cpp builds a kBackendUnavailable stub unless VB_WITH_NET is set
# (GameNetworkingSockets::static is linked from src/core/CMakeLists.txt under
# that flag).

target_sources(vb_core PRIVATE
  loopback.cpp
  gns_transport.cpp
  handshake.cpp
  session.cpp
  integrated.cpp
  world_replicator.cpp
)
