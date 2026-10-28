add_library(key_rebind SHARED key_rebind.cpp rebind_parse.cpp ../dev-console/il2cpp.cpp)
target_include_directories(key_rebind PRIVATE ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(key_rebind PRIVATE lazarus_core)

set(KR_OUT ${CMAKE_BINARY_DIR}/dist/Lazarus/mods/key-rebind)
set_target_properties(key_rebind PROPERTIES RUNTIME_OUTPUT_DIRECTORY $<1:${KR_OUT}>)
add_custom_command(TARGET key_rebind POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_CURRENT_SOURCE_DIR}/mod.ini ${KR_OUT}/mod.ini
  COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_CURRENT_SOURCE_DIR}/config.ini ${KR_OUT}/config.ini)
