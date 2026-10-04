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
if(WIN32)
  target_link_libraries(vb_automation PRIVATE ws2_32)
endif()
