# vb — developer CLI (architecture_spec/dev-cli.md). `vb_cli_lib` holds all the
# logic so vb_tests can exercise it without spawning the binary.

add_library(vb_cli_lib STATIC
  commands.cpp
  layout.cpp
  process.cpp
  store.cpp
  version.cpp
)

target_include_directories(vb_cli_lib PUBLIC "${CMAKE_SOURCE_DIR}/inc")

target_link_libraries(vb_cli_lib
  PUBLIC
    vb_core
  PRIVATE
    tomlplusplus::tomlplusplus
    vb_warnings
    vb_sanitizers
)

add_executable(vb main.cpp)
target_link_libraries(vb PRIVATE vb_cli_lib vb_warnings vb_sanitizers)
set_target_properties(vb PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
