# vb_auth — in-engine authentication (architecture_spec/auth.md §9).
# config.cpp: the auth.lua loader/validator (always built). The rest is the
# server-side verifier (Mbed TLS signature checks + libcurl HTTPS fetch) behind
# VB_WITH_AUTH; without it every primitive fails closed.

add_library(vb_auth STATIC
  config.cpp
  jwt.cpp
  crypto.cpp
  http.cpp
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
  target_sources(vb_auth PRIVATE jwks.cpp verifier.cpp service.cpp signin.cpp
    oidc_client.cpp firebase.cpp coordinator.cpp)
  target_compile_definitions(vb_auth PUBLIC VB_WITH_AUTH=1)
  target_link_libraries(vb_auth PRIVATE
    MbedTLS::mbedcrypto
    CURL::libcurl
    nlohmann_json::nlohmann_json)
  if(WIN32)
    target_link_libraries(vb_auth PRIVATE ws2_32 shell32)
  endif()
endif()
