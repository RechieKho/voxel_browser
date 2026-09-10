# cmake/Sanitizers.cmake
#
# vb_sanitizers — INTERFACE target applying the sanitizer flags selected by
# -DVB_ENABLE_ASAN / -DVB_ENABLE_UBSAN / -DVB_ENABLE_TSAN. Link it PRIVATE from
# first-party targets. TSan is mutually exclusive with ASan/UBSan.

if(TARGET vb_sanitizers)
  return()
endif()

add_library(vb_sanitizers INTERFACE)

set(_vb_san_flags "")

if(VB_ENABLE_TSAN AND (VB_ENABLE_ASAN OR VB_ENABLE_UBSAN))
  message(FATAL_ERROR "VB_ENABLE_TSAN cannot be combined with ASan/UBSan")
endif()

if(MSVC)
  if(VB_ENABLE_ASAN)
    list(APPEND _vb_san_flags /fsanitize=address)
  endif()
  if(VB_ENABLE_UBSAN OR VB_ENABLE_TSAN)
    message(WARNING "UBSan/TSan are not supported with MSVC; ignoring")
  endif()
else()
  if(VB_ENABLE_ASAN)
    list(APPEND _vb_san_flags -fsanitize=address -fno-omit-frame-pointer)
  endif()
  if(VB_ENABLE_UBSAN)
    list(APPEND _vb_san_flags -fsanitize=undefined -fno-omit-frame-pointer)
  endif()
  if(VB_ENABLE_TSAN)
    list(APPEND _vb_san_flags -fsanitize=thread -fno-omit-frame-pointer)
  endif()
endif()

if(_vb_san_flags)
  message(STATUS "vb: sanitizer flags ${_vb_san_flags}")
  target_compile_options(vb_sanitizers INTERFACE ${_vb_san_flags})
  if(NOT MSVC)
    target_link_options(vb_sanitizers INTERFACE ${_vb_san_flags})
  endif()
endif()
