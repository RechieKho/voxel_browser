# vb/world — voxel data model (spec §5.3). Compiled into vb_core.

target_sources(vb_core PRIVATE
  block.cpp
  lighting.cpp
  paletted_chunk_store.cpp
  world.cpp
)
