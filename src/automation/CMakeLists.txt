# vb_automation — development-only automation driver (docs/e2e-automation.md).
# Only added when VB_WITH_AUTOMATION=ON (src/CMakeLists.txt); it is linked into
# no target otherwise, so production binaries contain none of it.

add_library(vb_automation STATIC
  host.cpp
  predicate.cpp
  protocol.cpp
  tcp.cpp
)
add_library(vb::automation ALIAS vb_automation)

target_link_libraries(vb_automation
  PUBLIC
    vb_core
    nlohmann_json::nlohmann_json
  PRIVATE
    vb_warnings
    vb_sanitizers
)
target_compile_definitions(vb_automation PUBLIC VB_WITH_AUTOMATION=1)

# GCC 13 reports -Wmaybe-uninitialized from inside libstdc++'s <regex> (std::function in
# _State) when this file is built with ASan at -O2; with -Werror that fails the CI e2e job.
# A false positive in a system header, so silence just that warning for just this file.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  set_source_files_properties(predicate.cpp PROPERTIES COMPILE_OPTIONS "-Wno-maybe-uninitialized")
endif()
if(WIN32)
  target_link_libraries(vb_automation PRIVATE ws2_32)
endif()
