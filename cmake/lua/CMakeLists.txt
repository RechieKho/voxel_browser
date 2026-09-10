# Minimal CMake wrapper around PUC-Lua (which ships no build system).
# Expects the Lua sources to have been fetched to ${lua_src_SOURCE_DIR} by
# cmake/Dependencies.cmake. Produces a static `lua_static` + `lua::lua` alias.

if(TARGET lua::lua)
  return()
endif()

set(_lua_dir "${lua_src_SOURCE_DIR}")
if(NOT EXISTS "${_lua_dir}/lua.h")
  message(FATAL_ERROR "Lua sources not found at ${_lua_dir}")
endif()

file(GLOB _lua_sources "${_lua_dir}/*.c")
list(REMOVE_ITEM _lua_sources
  "${_lua_dir}/lua.c"   # stand-alone interpreter entry point
  "${_lua_dir}/luac.c"  # bytecode compiler entry point
  "${_lua_dir}/onelua.c")

add_library(lua_static STATIC ${_lua_sources})
target_include_directories(lua_static PUBLIC "${_lua_dir}")

if(UNIX AND NOT APPLE)
  target_compile_definitions(lua_static PRIVATE LUA_USE_LINUX)
  target_link_libraries(lua_static PUBLIC m ${CMAKE_DL_LIBS})
elseif(APPLE)
  target_compile_definitions(lua_static PRIVATE LUA_USE_MACOSX)
  target_link_libraries(lua_static PUBLIC m ${CMAKE_DL_LIBS})
endif()

set_target_properties(lua_static PROPERTIES C_STANDARD 99 POSITION_INDEPENDENT_CODE ON)
add_library(lua::lua ALIAS lua_static)
