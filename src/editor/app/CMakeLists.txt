add_executable(vb_structure_editor
  main.cpp
  editor_app.cpp
)

target_link_libraries(vb_structure_editor
  PRIVATE
    vb_editor_model
    vb_core
    vb_render
    vb_raygui
    vb_warnings
    vb_sanitizers
)

set_target_properties(vb_structure_editor PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)
