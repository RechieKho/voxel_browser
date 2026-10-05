# vb_auth — in-engine authentication (architecture_spec/auth.md §9).
# 9.1 ships only the auth.lua loader/validator; later steps add the token
# verifier (Mbed TLS + curl) behind VB_WITH_AUTH.

add_library(vb_auth STATIC
  config.cpp
)
add_library(vb::auth ALIAS vb_auth)

target_link_libraries(vb_auth
  PUBLIC
    vb_core
  PRIVATE
    vb_warnings
    vb_sanitizers
)

if(VB_WITH_AUTH)
  target_compile_definitions(vb_auth PUBLIC VB_WITH_AUTH=1)
endif()
