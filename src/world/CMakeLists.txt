# vb/world — voxel data model (spec §5.3). Compiled into vb_core.

target_sources(vb_core PRIVATE
  block.cpp
  chunk_codec.cpp
  chunk_lifecycle.cpp
  chunk_mesher.cpp
  client_chunk_store.cpp
  lighting.cpp
  paletted_chunk_store.cpp
  world.cpp
)
