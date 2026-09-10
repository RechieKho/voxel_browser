# vb/worldgen — terrain generation (spec §6). Compiled into vb_core.
# FastNoise2 (VB_WITH_WORLDGEN) becomes the Phase 4 Lua-pipeline backend; the
# fixed base pipeline uses the deterministic hand-rolled noise in vb/core.

target_sources(vb_core PRIVATE
  generator.cpp
  worker_pool.cpp
)

find_package(Threads REQUIRED)
target_link_libraries(vb_core PUBLIC Threads::Threads)
