# vb/script — embedded Lua 5.4 VM wrapper (spec §10). Compiled into vb_core.
# vm.cpp builds a kDisabled stub unless VB_WITH_LUA is set (lua::lua + sol2::sol2
# are linked from src/core/CMakeLists.txt under that flag).

target_sources(vb_core PRIVATE
  vm.cpp
)
