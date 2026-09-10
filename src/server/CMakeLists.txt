# voxel_browser_server — authoritative headless server executable.

add_executable(voxel_browser_server main.cpp)

target_link_libraries(voxel_browser_server
  PRIVATE
    vb_core
    vb_warnings
    vb_sanitizers
)

set_target_properties(voxel_browser_server PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)
