# vb/protocol — hand-written wire codecs. Compiled into vb_core.
# Every struct here has a round-trip test in tests/unit/protocol_test.cpp and
# bumps kEngineProtocolVersion + docs/protocol.md when its layout changes.

target_sources(vb_core PRIVATE
  handshake.cpp
  snapshot.cpp
  world.cpp
)
