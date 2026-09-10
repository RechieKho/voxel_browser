# Engine modules + executables. One add_subdirectory per module (spec §4).

add_subdirectory(core)

if(VB_BUILD_CLIENT)
  add_subdirectory(render)
endif()

if(VB_BUILD_SERVER)
  add_subdirectory(server)
endif()

if(VB_BUILD_CLIENT)
  add_subdirectory(client)
endif()
