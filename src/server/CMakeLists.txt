# voxel_browser_server — authoritative headless server executable.

add_executable(voxel_browser_server main.cpp)

# `--check-pack` (headless pack validation). Needs Lua; a build without it
# reports that instead.
if(VB_WITH_LUA)
  add_library(vb_pack_check STATIC check_pack.cpp)
  target_link_libraries(vb_pack_check PUBLIC vb_core vb_auth PRIVATE vb_warnings vb_sanitizers)
  target_include_directories(vb_pack_check PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
endif()

# GCC 13 reports -Wmaybe-uninitialized from inside libstdc++'s <regex> (std::function in
# _State) under ASan at -O2; with -Werror that fails the asan CI build. A false positive in a
# system header, so silence just that warning for just this file (same as src/automation).
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  set_source_files_properties(check_pack.cpp PROPERTIES COMPILE_OPTIONS "-Wno-maybe-uninitialized")
endif()

target_link_libraries(voxel_browser_server
  PRIVATE
    vb_core
    vb_auth
    vb_warnings
    vb_sanitizers
)
if(VB_WITH_LUA)
  target_link_libraries(voxel_browser_server PRIVATE vb_pack_check)
endif()

set_target_properties(voxel_browser_server PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

# Development-only automation channel; no automation code is linked otherwise.
if(VB_WITH_AUTOMATION)
  target_link_libraries(voxel_browser_server PRIVATE vb_automation)
endif()
