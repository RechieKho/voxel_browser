add_library(vb_editor_model STATIC
  block_catalog.cpp
  commands.cpp
  edit_session.cpp
  pick.cpp
  structure_doc.cpp
  structure_writer.cpp
  volume_view.cpp
  workspace.cpp
)
add_library(vb::editor_model ALIAS vb_editor_model)

target_include_directories(vb_editor_model PUBLIC "${CMAKE_SOURCE_DIR}/inc")
target_compile_features(vb_editor_model PUBLIC cxx_std_20)

target_link_libraries(vb_editor_model
  PUBLIC
    vb_core
  PRIVATE
    tomlplusplus::tomlplusplus
    vb_warnings
    vb_sanitizers
)
