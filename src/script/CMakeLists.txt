# vb/script — embedded Lua 5.4 VM wrapper (spec §10). Compiled into vb_core.
# vm.cpp builds a kDisabled stub unless VB_WITH_LUA is set (lua::lua + sol2::sol2
# are linked from src/core/CMakeLists.txt under that flag).

target_sources(vb_core PRIVATE
  db.cpp
  pack_loader.cpp
  pack_runtime.cpp
  ui_runtime.cpp
  vm.cpp
)

# pack_runtime.cpp's sol2 usertype/lambda bindings (PlayerHandle alone is 20+
# methods) push MSVC's object-file section count over its default limit --
# hit while adding the entity-management follow-up's get_selected_slot/
# get_held_item bindings ("fatal error C1128: number of sections exceeded
# object file format limit"). /bigobj lifts that cap; harmless on other
# compilers where the flag doesn't apply.
if(MSVC)
  # Source properties are only visible in the directory they're set in unless
  # TARGET_DIRECTORY says otherwise (CMake 3.18+) -- vb_core itself is
  # defined in src/core/CMakeLists.txt, not here, so that's required for this
  # to actually reach vb_core's build rule for this file.
  set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/pack_runtime.cpp
    TARGET_DIRECTORY vb_core
    PROPERTIES COMPILE_OPTIONS "/bigobj")
endif()
