add_library(dev_console SHARED dev_console.cpp commands.cpp il2cpp.cpp)
target_include_directories(dev_console PRIVATE ${PROJECT_SOURCE_DIR}/include)

set(CON_OUT ${CMAKE_BINARY_DIR}/dist/Lazarus/mods/dev-console)
set_target_properties(dev_console PROPERTIES RUNTIME_OUTPUT_DIRECTORY $<1:${CON_OUT}>)
add_custom_command(TARGET dev_console POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_CURRENT_SOURCE_DIR}/mod.ini ${CON_OUT}/mod.ini
  COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_CURRENT_SOURCE_DIR}/config.ini ${CON_OUT}/config.ini)
