# cmake/Warnings.cmake
#
# vb_warnings — INTERFACE target carrying the project's warning flags. Link it
# PRIVATE from first-party targets only (never from fetched dependencies).
#
#   target_link_libraries(vb_core PRIVATE vb_warnings)
#
# Warnings-as-errors is opt-in via -DVB_WARNINGS_AS_ERRORS=ON (CI sets it).

if(TARGET vb_warnings)
  return()
endif()

add_library(vb_warnings INTERFACE)

if(MSVC)
  target_compile_options(vb_warnings INTERFACE
    /W4
    /permissive-
    /wd4127 # conditional expression is constant (fires in <array>/templates)
    $<$<BOOL:${VB_WARNINGS_AS_ERRORS}>:/WX>
  )
else()
  target_compile_options(vb_warnings INTERFACE
    -Wall
    -Wextra
    -Wpedantic
    -Wshadow
    -Wconversion
    -Wsign-conversion
    -Wnon-virtual-dtor
    -Wold-style-cast
    -Wcast-align
    -Wunused
    -Woverloaded-virtual
    -Wdouble-promotion
    $<$<BOOL:${VB_WARNINGS_AS_ERRORS}>:-Werror>
    # sol2's proxy conversion operators (e.g. table_proxy -> object,
    # load_result -> protected_function) trip -Wconversion/-Wsign-conversion
    # at the call site even though sol2 itself is included as a system
    # header; EnTT's dense_map internals do the same for -Wsign-conversion.
    # Keep them as non-fatal warnings rather than gating CI on vendored code.
    $<$<BOOL:${VB_WARNINGS_AS_ERRORS}>:-Wno-error=conversion>
    $<$<BOOL:${VB_WARNINGS_AS_ERRORS}>:-Wno-error=sign-conversion>
    # GCC's -Wall implies -Warray-bounds at -O2+, which false-positives
    # inside sol2's stack_field.hpp string handling under release builds
    # (known upstream sol2/GCC interaction, not a real out-of-bounds access
    # in our code).
    $<$<BOOL:${VB_WARNINGS_AS_ERRORS}>:-Wno-error=array-bounds>
  )
endif()
