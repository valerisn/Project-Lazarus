add_library(skip_intro SHARED skip_intro.cpp ../dev-console/il2cpp.cpp)
target_include_directories(skip_intro PRIVATE ${PROJECT_SOURCE_DIR}/include)

set(SKIP_OUT ${CMAKE_BINARY_DIR}/dist/Lazarus/mods/skip-intro)
set_target_properties(skip_intro PROPERTIES RUNTIME_OUTPUT_DIRECTORY $<1:${SKIP_OUT}>)
add_custom_command(TARGET skip_intro POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_CURRENT_SOURCE_DIR}/mod.ini ${SKIP_OUT}/mod.ini
  COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_CURRENT_SOURCE_DIR}/config.ini ${SKIP_OUT}/config.ini)
