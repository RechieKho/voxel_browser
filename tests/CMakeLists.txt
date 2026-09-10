# vb_tests — unit + integration tests (doctest).
# Integration tests (headless client <-> server) are added from Phase 1.

add_executable(vb_tests
  main.cpp
  unit/cli_test.cpp
)

target_link_libraries(vb_tests
  PRIVATE
    vb_core
    doctest::doctest
    vb_warnings
    vb_sanitizers
)

set_target_properties(vb_tests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

add_test(NAME vb_tests COMMAND vb_tests --no-intro --force-colors=off)

# Smoke tests: the binaries start, print, and exit cleanly headless.
if(VB_BUILD_SERVER)
  add_test(NAME server_smoke COMMAND voxel_browser_server --ticks 2 --tick-rate 60)
endif()
if(VB_BUILD_CLIENT)
  add_test(NAME client_smoke COMMAND voxel_browser --headless --frames 2)
endif()
