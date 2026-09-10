# Voxel Browser — top-level build.
#
# Layout (see ARCHITECTURE_SPEC.md §4):
#   vb_core                STATIC  engine core (no rendering, headless-capable)
#   vb_render              STATIC  client-only rendering (raylib + raygui + Cellulose)
#   voxel_browser_server   EXE     authoritative headless server
#   voxel_browser          EXE     client ("the browser")
#   vb_tests               EXE     unit + integration tests (doctest)
#
# Heavy dependencies (GameNetworkingSockets, librg, Lua, sol2, FastNoise2,
# Cellulose, LZ4, xxHash) are declared in cmake/Dependencies.cmake and gated
# behind VB_WITH_* options so each implementation phase can turn on only what
# it needs. Phase 0 builds green with just raylib + raygui + EnTT + doctest.

cmake_minimum_required(VERSION 3.25)

# --- Version (from git tags, single source of truth) --------------------------
find_package(Git QUIET)
if(GIT_FOUND)
  execute_process(
    COMMAND ${GIT_EXECUTABLE} describe --tags --dirty --always
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_TAG_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
  )
endif()
if(NOT GIT_TAG_VERSION)
  set(GIT_TAG_VERSION "0.0.0")
endif()

# Strip a leading "v" for project(VERSION); keep the descriptor verbatim for
# the banner. A checkout with no version tag yet (just a commit hash from
# `--always`) falls back to 0.0.0 — the first `git tag v0.0.1` fixes this.
string(REGEX REPLACE "^v" "" VB_VERSION_FULL "${GIT_TAG_VERSION}")
if(VB_VERSION_FULL MATCHES "^([0-9]+\\.[0-9]+(\\.[0-9]+)?)")
  set(VB_VERSION_NUMERIC "${CMAKE_MATCH_1}")
else()
  set(VB_VERSION_NUMERIC "0.0.0")
endif()
message(STATUS "vb: version ${VB_VERSION_FULL} (numeric ${VB_VERSION_NUMERIC})")

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/PROJECT_NAME" PROJECT_NAME)
string(STRIP "${PROJECT_NAME}" PROJECT_NAME)
message(STATUS "vb: project name ${PROJECT_NAME}")

project(${PROJECT_NAME}
  VERSION ${VB_VERSION_NUMERIC}
  LANGUAGES CXX C
)

# --- Global settings ---------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE "RelWithDebInfo" CACHE STRING "" FORCE)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

get_directory_property(VB_HAS_PARENT PARENT_DIRECTORY)

# --- Options ---------------------------------------------------------------
option(VB_BUILD_CLIENT   "Build the client executable (voxel_browser)"        ON)
option(VB_BUILD_SERVER   "Build the server executable (voxel_browser_server)" ON)
option(VB_BUILD_TESTS    "Build the test executable (vb_tests)"               ON)
option(VB_HEADLESS       "Force a headless client build (no GPU/window)"      OFF)
option(VB_WARNINGS_AS_ERRORS "Treat compiler warnings as errors"             OFF)

option(VB_ENABLE_ASAN    "Enable AddressSanitizer"                            OFF)
option(VB_ENABLE_UBSAN   "Enable UndefinedBehaviorSanitizer"                  OFF)
option(VB_ENABLE_TSAN    "Enable ThreadSanitizer"                             OFF)

# Dependency gates — flipped on per implementation phase.
option(VB_WITH_NET       "GameNetworkingSockets transport (Phase 1)"          OFF)
option(VB_WITH_REPLICATION "librg replication (Phase 1)"                      OFF)
option(VB_WITH_LUA       "Lua 5.4 + sol2 scripting (Phase 4)"                 OFF)
option(VB_WITH_WORLDGEN  "FastNoise2 world generation (Phase 2)"              OFF)
option(VB_WITH_COMPRESSION "LZ4 + xxHash chunk/asset codecs (Phase 2/4)"      OFF)
option(VB_WITH_MESHING   "Cellulose voxel meshing (Phase 2)"                  OFF)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  set(VB_TESTS_DEFAULT_HEADLESS ON)
endif()

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/Sanitizers.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/Warnings.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/Dependencies.cmake)

# --- Generated version header ----------------------------------------------
set(VB_GENERATED_INCLUDE_DIR "${CMAKE_BINARY_DIR}/generated/include")
string(TIMESTAMP VB_BUILD_TIMESTAMP "%Y-%m-%dT%H:%M:%SZ" UTC)
configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake/version.hpp.in"
  "${VB_GENERATED_INCLUDE_DIR}/vb/core/version.hpp"
  @ONLY
)

# --- Modules --------------------------------------------------------------
add_subdirectory(src)

if(VB_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

if(VB_HAS_PARENT)
  set(${PROJECT_NAME}_CORE_LIBRARY "vb_core" PARENT_SCOPE)
endif()
