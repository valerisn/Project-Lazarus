# Builds a fake "game folder" in build/e2e_stage that mirrors the real one:
#   fake_game.exe -> UnityPlayer.dll -> VERSION.dll (our proxy) ... GameAssembly.dll
set(STAGE ${CMAKE_BINARY_DIR}/e2e_stage)
set(MODDIR ${STAGE}/Lazarus/mods/test-mod)

add_library(fake_gameassembly SHARED fake_gameassembly.cpp)
set_target_properties(fake_gameassembly PROPERTIES OUTPUT_NAME GameAssembly)

add_library(fake_unityplayer SHARED fake_unityplayer.cpp)
set_target_properties(fake_unityplayer PROPERTIES OUTPUT_NAME UnityPlayer)

add_executable(fake_game fake_game.cpp)
target_link_libraries(fake_game PRIVATE fake_unityplayer)

add_library(test_mod SHARED test_mod.cpp)
target_include_directories(test_mod PRIVATE ${PROJECT_SOURCE_DIR}/include)

add_executable(e2e_runner e2e_runner.cpp)
add_dependencies(e2e_runner fake_game fake_gameassembly test_mod lazarus_proxy)

foreach(t fake_gameassembly fake_unityplayer fake_game e2e_runner)
  set_target_properties(${t} PROPERTIES RUNTIME_OUTPUT_DIRECTORY $<1:${STAGE}>)
endforeach()
set_target_properties(test_mod PROPERTIES RUNTIME_OUTPUT_DIRECTORY $<1:${MODDIR}>)

# the real proxy gets copied in as version.dll, exactly what the installer does
add_custom_command(TARGET e2e_runner POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:lazarus_proxy> ${STAGE}/version.dll)

add_test(NAME e2e_bootstrap COMMAND ${STAGE}/e2e_runner.exe)

# the real graphics-settings mod, dropped in the way a player would install it
add_dependencies(e2e_runner graphics_settings)
add_custom_command(TARGET e2e_runner POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:graphics_settings> ${STAGE}/Lazarus/mods/graphics-settings/graphics_settings.dll
  COMMAND ${CMAKE_COMMAND} -E copy_if_different ${PROJECT_SOURCE_DIR}/mods/graphics-settings/mod.ini ${STAGE}/Lazarus/mods/graphics-settings/mod.ini)

add_dependencies(e2e_runner fps_counter)
add_custom_command(TARGET e2e_runner POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:fps_counter> ${STAGE}/Lazarus/mods/fps-counter/fps_counter.dll
  COMMAND ${CMAKE_COMMAND} -E copy_if_different ${PROJECT_SOURCE_DIR}/mods/fps-counter/mod.ini ${STAGE}/Lazarus/mods/fps-counter/mod.ini
  COMMAND ${CMAKE_COMMAND} -E copy_if_different ${PROJECT_SOURCE_DIR}/mods/fps-counter/config.ini ${STAGE}/Lazarus/mods/fps-counter/config.ini)

add_dependencies(e2e_runner dev_console)
add_custom_command(TARGET e2e_runner POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:dev_console> ${STAGE}/Lazarus/mods/dev-console/dev_console.dll
  COMMAND ${CMAKE_COMMAND} -E copy_if_different ${PROJECT_SOURCE_DIR}/mods/dev-console/mod.ini ${STAGE}/Lazarus/mods/dev-console/mod.ini)
