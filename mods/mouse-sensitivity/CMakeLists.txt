add_library(mouse_sensitivity SHARED mouse_sensitivity.cpp ../dev-console/il2cpp.cpp)
target_include_directories(mouse_sensitivity PRIVATE ${PROJECT_SOURCE_DIR}/include)

set(MS_OUT ${CMAKE_BINARY_DIR}/dist/Lazarus/mods/mouse-sensitivity)
set_target_properties(mouse_sensitivity PROPERTIES RUNTIME_OUTPUT_DIRECTORY $<1:${MS_OUT}>)
add_custom_command(TARGET mouse_sensitivity POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_CURRENT_SOURCE_DIR}/mod.ini ${MS_OUT}/mod.ini
  COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_CURRENT_SOURCE_DIR}/config.ini ${MS_OUT}/config.ini)
