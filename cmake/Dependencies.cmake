# cmake/Dependencies.cmake
#
# One place for every third-party dependency. Each block tries find_package()
# first (distro / vcpkg / system install) and falls back to a pinned
# FetchContent checkout, mirroring the raylib pattern from the original skeleton.
#
# Heavy or spike-blocked dependencies are only *declared* here and are pulled in
# lazily by the VB_WITH_* option that owns them:
#
#   VB_WITH_NET         -> GameNetworkingSockets   (Phase 1.2 transport)
#   VB_WITH_REPLICATION -> librg + zpl             (Phase 1.4 replication spike)
#   VB_WITH_WORLDGEN    -> FastNoise2              (Phase 2.2 terrain)
#   VB_WITH_COMPRESSION -> lz4 + xxHash            (Phase 2.4 / 4.4 codecs)
#   VB_WITH_LUA         -> Lua 5.4 + sol2          (Phase 4 scripting)
#   VB_WITH_MESHING     -> Cellulose               (Phase 2.5 meshing spike)
#
# Keep the pinned tags in sync with docs/protocol.md / STATE.md when bumped.

include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

# Some pinned dependencies (doctest v2.4.11) still declare
# cmake_minimum_required(VERSION 3.0), which CMake >= 4.0 rejects outright.
# Give those sub-builds a floor so they configure under modern CMake.
if(CMAKE_VERSION VERSION_GREATER_EQUAL 4.0 AND NOT DEFINED CMAKE_POLICY_VERSION_MINIMUM)
  set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
endif()

# ---------------------------------------------------------------------------
# vb_fetch(<name> TAG <git-tag> REPO <url> [SUBDIR <dir>])
#   Declares a dependency and makes it available now. SUBDIR points
#   FetchContent at a nested CMakeLists.txt (used for the Lua wrapper).
# ---------------------------------------------------------------------------
function(vb_fetch NAME)
  cmake_parse_arguments(ARG "" "TAG;REPO;SUBDIR" "" ${ARGN})
  set(_extra "")
  if(ARG_SUBDIR)
    set(_extra SOURCE_SUBDIR ${ARG_SUBDIR})
  endif()
  FetchContent_Declare(${NAME}
    GIT_REPOSITORY ${ARG_REPO}
    GIT_TAG ${ARG_TAG}
    GIT_SHALLOW TRUE
    GIT_PROGRESS TRUE
    ${_extra}
  )
  FetchContent_MakeAvailable(${NAME})
endfunction()

if(VB_BUILD_CLIENT)

# ===========================================================================
# raylib — window / GL context / input (needed by vb_render only)
# ===========================================================================
find_package(raylib 5.5 QUIET)
if(NOT raylib_FOUND)
  set(BUILD_EXAMPLES OFF CACHE INTERNAL "")
  set(BUILD_GAMES OFF CACHE INTERNAL "")
  set(CUSTOMIZE_BUILD ON CACHE INTERNAL "")
  set(SUPPORT_MODULE_RAUDIO OFF CACHE INTERNAL "") # no audio subsystem in v0
  vb_fetch(raylib TAG 5.5 REPO https://github.com/raysan5/raylib.git)
endif()

# ===========================================================================
# raygui — immediate-mode UI (header-only; implementation TU lives in vb_render)
# ===========================================================================
if(NOT TARGET vb_raygui)
  FetchContent_Declare(raygui
    GIT_REPOSITORY https://github.com/raysan5/raygui.git
    GIT_TAG 4.0
    GIT_SHALLOW TRUE
  )
  FetchContent_MakeAvailable(raygui)
  add_library(vb_raygui INTERFACE)
  target_include_directories(vb_raygui SYSTEM INTERFACE "${raygui_SOURCE_DIR}/src")
  target_link_libraries(vb_raygui INTERFACE raylib)
  add_library(vb::raygui ALIAS vb_raygui)
endif()

endif() # VB_BUILD_CLIENT

# ===========================================================================
# EnTT — ECS (header-only, used by vb_core)
# ===========================================================================
find_package(EnTT QUIET)
if(NOT EnTT_FOUND AND NOT TARGET EnTT::EnTT)
  vb_fetch(entt TAG v3.13.2 REPO https://github.com/skypjack/entt.git)
endif()

# ===========================================================================
# tomlplusplus — server.toml / client.toml config loader (Phase 1.1)
# ===========================================================================
find_package(tomlplusplus QUIET)
if(NOT tomlplusplus_FOUND AND NOT TARGET tomlplusplus::tomlplusplus)
  vb_fetch(tomlplusplus TAG v3.4.0
    REPO https://github.com/marzer/tomlplusplus.git)
endif()

# ===========================================================================
# doctest — test framework (Phase 0 test target)
# ===========================================================================
if(VB_BUILD_TESTS)
  find_package(doctest QUIET)
  if(NOT doctest_FOUND AND NOT TARGET doctest::doctest)
    set(DOCTEST_WITH_TESTS OFF CACHE INTERNAL "")
    set(DOCTEST_NO_INSTALL ON CACHE INTERNAL "")
    vb_fetch(doctest TAG v2.4.11 REPO https://github.com/doctest/doctest.git)
  endif()
endif()

# ===========================================================================
# GameNetworkingSockets — reliable/unreliable UDP transport (Phase 1)
#   Transitive: protobuf + a crypto backend (OpenSSL or libsodium/BCrypt).
#   Prefer a system install; the FetchContent path also pulls protobuf.
# ===========================================================================
if(VB_WITH_NET)
  find_package(GameNetworkingSockets QUIET)
  if(NOT GameNetworkingSockets_FOUND)
    message(STATUS "vb: fetching GameNetworkingSockets (pulls protobuf; needs OpenSSL)")
    set(BUILD_EXAMPLES OFF CACHE INTERNAL "")
    set(BUILD_TESTS OFF CACHE INTERNAL "")
    vb_fetch(gamenetworkingsockets
      TAG v1.4.1
      REPO https://github.com/ValveSoftware/GameNetworkingSockets.git)
  endif()
endif()

# ===========================================================================
# librg — interest management / entity streaming (spec §19 Q3, docs/replication.md)
#   v7.4.0 is a single self-contained header (code/librg.h bundles its own zpl —
#   no separate zpl dependency). LIBRG_IMPL goes in exactly one TU, built as its
#   own target so the project warning flags don't touch the C code.
# ===========================================================================
if(VB_WITH_REPLICATION AND NOT TARGET vb_librg)
  FetchContent_Declare(librg
    GIT_REPOSITORY https://github.com/zpl-c/librg.git
    GIT_TAG v7.4.0 GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(librg)
  add_library(vb_librg INTERFACE)
  target_include_directories(vb_librg SYSTEM INTERFACE "${librg_SOURCE_DIR}/code")
  add_library(vb::librg ALIAS vb_librg)
endif()

# ===========================================================================
# FastNoise2 — world generation noise trees (Phase 2)
# ===========================================================================
if(VB_WITH_WORLDGEN)
  find_package(FastNoise2 QUIET)
  if(NOT FastNoise2_FOUND)
    set(FASTNOISE2_NOISETOOL OFF CACHE INTERNAL "")
    set(FASTNOISE2_TESTS OFF CACHE INTERNAL "")
    vb_fetch(fastnoise2 TAG v0.10.0 REPO https://github.com/Auburn/FastNoise2.git)
  endif()
endif()

# ===========================================================================
# lz4 + xxHash — chunk / asset compression + content hashing (Phase 2/4)
# ===========================================================================
if(VB_WITH_COMPRESSION)
  find_package(lz4 QUIET)
  if(NOT lz4_FOUND AND NOT TARGET LZ4::lz4)
    set(LZ4_BUILD_CLI OFF CACHE INTERNAL "")
    set(LZ4_BUILD_LEGACY_LZ4C OFF CACHE INTERNAL "")
    FetchContent_Declare(lz4
      GIT_REPOSITORY https://github.com/lz4/lz4.git
      GIT_TAG v1.9.4 GIT_SHALLOW TRUE
      SOURCE_SUBDIR build/cmake)
    FetchContent_MakeAvailable(lz4)
  endif()

  find_package(xxHash QUIET)
  if(NOT xxHash_FOUND AND NOT TARGET xxHash::xxhash)
    set(XXHASH_BUILD_XXHSUM OFF CACHE INTERNAL "")
    FetchContent_Declare(xxhash
      GIT_REPOSITORY https://github.com/Cyan4973/xxHash.git
      GIT_TAG v0.8.2 GIT_SHALLOW TRUE
      SOURCE_SUBDIR cmake_unofficial)
    FetchContent_MakeAvailable(xxhash)
  endif()
endif()

# ===========================================================================
# Lua 5.4 + sol2 — scripting (Phase 4). Binding layer decision: sol2 (§19 Q1).
#   PUC-Lua ships no CMake, so cmake/lua/CMakeLists.txt wraps it into a
#   static `lua_static` target with a `lua::lua` alias.
# ===========================================================================
if(VB_WITH_LUA)
  find_package(Lua 5.4 QUIET)
  if(NOT LUA_FOUND AND NOT TARGET lua::lua)
    FetchContent_Declare(lua_src
      GIT_REPOSITORY https://github.com/lua/lua.git
      GIT_TAG v5.4.6 GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(lua_src)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/cmake/lua" "${CMAKE_BINARY_DIR}/lua_wrapper")
  endif()

  find_package(sol2 QUIET)
  if(NOT sol2_FOUND AND NOT TARGET sol2::sol2)
    set(SOL2_BUILD_LUA OFF CACHE INTERNAL "")
    # v3.3.0's bundled "better optional" doesn't compile under Clang >= 18
    # (no member 'construct' in optional<T&>); v3.5.0 fixes it.
    vb_fetch(sol2 TAG v3.5.0 REPO https://github.com/ThePhD/sol2.git)
  endif()
endif()

# ===========================================================================
# Cellulose — voxel meshing (spec §19 Q2, resolved). Header-only; we use its
# greedy_mesh(vector<MeshSample>, ...) entry point. It vendors unordered_dense
# as a submodule and its demo would re-fetch raylib, so: pull submodules and
# turn the demo off. Phase 2 ships vb/render/chunk_mesher (same I/O) with this
# OFF; flip VB_WITH_MESHING to swap in greedy_mesh.
# ===========================================================================
if(VB_WITH_MESHING AND NOT TARGET cellulose)
  set(CELLULOSE_BUILD_DEMO OFF CACHE INTERNAL "")
  FetchContent_Declare(cellulose
    GIT_REPOSITORY https://github.com/RechieKho/cellulose.git
    GIT_TAG main
    GIT_SHALLOW TRUE
    GIT_SUBMODULES_RECURSE TRUE)
  FetchContent_MakeAvailable(cellulose)
endif()
