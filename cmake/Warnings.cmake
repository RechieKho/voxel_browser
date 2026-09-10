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
  )
endif()
