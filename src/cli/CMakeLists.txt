# vb — developer CLI (architecture_spec/dev-cli.md). `vb_cli_lib` holds all the
# logic so vb_tests can exercise it without spawning the binary.

add_library(vb_cli_lib STATIC
  commands.cpp
  installer.cpp
  instance.cpp
  layout.cpp
  lock.cpp
  portcheck.cpp
  process.cpp
  release_manifest.cpp
  source.cpp
  store.cpp
  version.cpp
  zip.cpp
)

target_include_directories(vb_cli_lib PUBLIC "${CMAKE_SOURCE_DIR}/inc")

target_link_libraries(vb_cli_lib
  PUBLIC
    vb_core
  PRIVATE
    tomlplusplus::tomlplusplus
    CURL::libcurl
    miniz
    vb_warnings
    vb_sanitizers
)

add_executable(vb main.cpp)
target_link_libraries(vb PRIVATE vb_cli_lib vb_warnings vb_sanitizers)
set_target_properties(vb PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

if(WIN32)
  target_link_libraries(vb_cli_lib PRIVATE ws2_32) # portcheck.cpp
endif()

# miniz's own include dir is not SYSTEM; keep -Werror away from third-party code.
get_target_property(_vb_miniz_inc miniz INTERFACE_INCLUDE_DIRECTORIES)
if(_vb_miniz_inc)
  target_include_directories(vb_cli_lib SYSTEM PRIVATE ${_vb_miniz_inc})
endif()
