# vb_render — client-only rendering. Links raylib + raygui (+ Cellulose later).
# Never linked by the server. Builds in headless mode too (window.cpp no-ops).

# raygui implementation isolated so -Werror / /WX never touches third-party code.
add_library(vb_raygui_impl STATIC raygui_impl.c)
target_link_libraries(vb_raygui_impl PUBLIC vb_raygui)
set_target_properties(vb_raygui_impl PROPERTIES C_STANDARD 99)

add_library(vb_render STATIC
  window.cpp
)
add_library(vb::render ALIAS vb_render)

target_include_directories(vb_render PUBLIC "${CMAKE_SOURCE_DIR}/inc")
target_compile_features(vb_render PUBLIC cxx_std_20)

target_link_libraries(vb_render
  PUBLIC
    vb_core
    raylib
    vb_raygui
    vb_raygui_impl
  PRIVATE
    vb_warnings
    vb_sanitizers
)

if(VB_WITH_MESHING)
  target_link_libraries(vb_render PUBLIC cellulose)
  target_compile_definitions(vb_render PUBLIC VB_WITH_MESHING=1)
endif()
