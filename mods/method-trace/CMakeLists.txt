add_library(method_trace SHARED method_trace.cpp trace_config.cpp)
target_include_directories(method_trace PRIVATE ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(method_trace PRIVATE lazarus_core)

set(MT_OUT ${CMAKE_BINARY_DIR}/dist/Lazarus/mods/method-trace)
set_target_properties(method_trace PROPERTIES RUNTIME_OUTPUT_DIRECTORY $<1:${MT_OUT}>)
add_custom_command(TARGET method_trace POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_CURRENT_SOURCE_DIR}/mod.ini ${MT_OUT}/mod.ini
  COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_CURRENT_SOURCE_DIR}/config.ini ${MT_OUT}/config.ini)
