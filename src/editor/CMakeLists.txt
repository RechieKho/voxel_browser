# Structure editor (docs/structure-editor.md §J).
#   vb_editor_model        STATIC  headless model: volume, structure document,
#                                  writer, block catalog, workspace. Built
#                                  whenever tests are, so CI covers it
#                                  everywhere.
#   vb_structure_editor    EXE     the raylib/raygui app (VB_BUILD_EDITOR).

add_subdirectory(model)

if(VB_BUILD_EDITOR)
  add_subdirectory(app)
endif()
