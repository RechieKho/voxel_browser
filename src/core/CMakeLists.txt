# vb_core — engine core. No rendering, no window; must build and run headless.
# Later phases add sources here (net/, world/, worldgen/, ecs/, replication/,
# assetsync/, script/, protocol/) under this same target.

add_library(vb_core STATIC
  build_info.cpp
  cli.cpp
  config.cpp
  log.cpp
)
add_library(vb::core ALIAS vb_core)

# Sibling modules attach their sources to vb_core via target_sources()
# (see src/CMakeLists.txt add_subdirectory order).

target_include_directories(vb_core PUBLIC
  "${CMAKE_SOURCE_DIR}/inc"
  "${VB_GENERATED_INCLUDE_DIR}"
)

target_compile_features(vb_core PUBLIC cxx_std_20)

target_link_libraries(vb_core
  PUBLIC
    EnTT::EnTT
  PRIVATE
    tomlplusplus::tomlplusplus
    vb_warnings
    vb_sanitizers
)

if(VB_WITH_NET)
  target_link_libraries(vb_core PUBLIC GameNetworkingSockets::GameNetworkingSockets)
  target_compile_definitions(vb_core PUBLIC VB_WITH_NET=1)
endif()

if(VB_WITH_REPLICATION)
  target_link_libraries(vb_core PUBLIC librg)
  target_compile_definitions(vb_core PUBLIC VB_WITH_REPLICATION=1)
endif()

if(VB_WITH_WORLDGEN)
  target_link_libraries(vb_core PUBLIC FastNoise2)
  target_compile_definitions(vb_core PUBLIC VB_WITH_WORLDGEN=1)
endif()

if(VB_WITH_COMPRESSION)
  if(TARGET LZ4::lz4)
    target_link_libraries(vb_core PUBLIC LZ4::lz4)
  elseif(TARGET lz4_static)
    target_link_libraries(vb_core PUBLIC lz4_static)
  endif()
  if(TARGET xxHash::xxhash)
    target_link_libraries(vb_core PUBLIC xxHash::xxhash)
  elseif(TARGET xxhash)
    target_link_libraries(vb_core PUBLIC xxhash)
  endif()
  target_compile_definitions(vb_core PUBLIC VB_WITH_COMPRESSION=1)
endif()

if(VB_WITH_LUA)
  target_link_libraries(vb_core PUBLIC lua::lua sol2::sol2)
  target_compile_definitions(vb_core PUBLIC VB_WITH_LUA=1)
endif()
