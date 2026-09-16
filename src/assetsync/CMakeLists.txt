# vb/assetsync — asset sync protocol (spec §9, Phase 4.4). Compiled into
# vb_core. Both files build a kDisabled stub unless VB_WITH_COMPRESSION is
# set (xxHash is linked from src/core/CMakeLists.txt under that flag).

target_sources(vb_core PRIVATE
  cache.cpp
  manifest.cpp
)
