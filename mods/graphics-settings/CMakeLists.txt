add_library(graphics_settings SHARED graphics_settings.cpp settings.cpp)
target_include_directories(graphics_settings PRIVATE ${PROJECT_SOURCE_DIR}/include)

# config.ini comes from kDefaultConfig so the file and the parser can't disagree
add_executable(gfx_write_default_config write_default_config.cpp settings.cpp)

set(GFX_OUT ${CMAKE_BINARY_DIR}/dist/Lazarus/mods/graphics-settings)
set_target_properties(graphics_settings PROPERTIES RUNTIME_OUTPUT_DIRECTORY $<1:${GFX_OUT}>)
add_custom_command(TARGET graphics_settings POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_CURRENT_SOURCE_DIR}/mod.ini ${GFX_OUT}/mod.ini
  COMMAND $<TARGET_FILE:gfx_write_default_config> ${GFX_OUT}/config.ini)
add_dependencies(graphics_settings gfx_write_default_config)
