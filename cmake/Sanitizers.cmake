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
    # MSVC's ASan auto-enables STL container-overflow annotations
    # (annotate_string/annotate_vector/annotate_optional), which bakes an
    # ABI-affecting macro into every TU that includes <vector>/<string>/
    # <optional>. Only first-party targets link vb_sanitizers (see the file
    # comment above) -- FetchContent'd third-party libs like
    # GameNetworkingSockets are never recompiled with /fsanitize=address, so
    # their .obj files disagree with ours on these macros and the linker
    # refuses with LNK2038 ("mismatch detected for 'annotate_vector'").
    # Disabling the annotations trades away ASan's container-overflow checks
    # (still get heap-buffer-overflow/use-after-free/etc. everywhere else)
    # for the ability to link against non-instrumented static libs at all.
    target_compile_definitions(vb_sanitizers INTERFACE
      _DISABLE_VECTOR_ANNOTATION
      _DISABLE_STRING_ANNOTATION
      _DISABLE_OPTIONAL_ANNOTATION)
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
