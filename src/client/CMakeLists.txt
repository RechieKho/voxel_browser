# voxel_browser — client executable ("the browser").

add_executable(voxel_browser main.cpp)

target_link_libraries(voxel_browser
  PRIVATE
    vb_core
    vb_render
    vb_raygui
    vb_warnings
    vb_sanitizers
)

# VB_HEADLESS forces a build that never touches the GPU (CI / tests).
target_compile_definitions(voxel_browser
  PRIVATE VB_HEADLESS_DEFAULT=$<BOOL:${VB_HEADLESS}>
)

set_target_properties(voxel_browser PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)
