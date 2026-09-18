# vb/world — voxel data model (spec §5.3). Compiled into vb_core.

target_sources(vb_core PRIVATE
  block.cpp
  block_damage.cpp
  chunk_codec.cpp
  chunk_lifecycle.cpp
  chunk_mesher.cpp
  chunk_mesh_snapshot.cpp
  chunk_mesh_worker_pool.cpp
  client_chunk_store.cpp
  daynight.cpp
  item_drops.cpp
  lighting.cpp
  paletted_chunk_store.cpp
  raycast.cpp
  world.cpp
)
