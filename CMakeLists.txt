cmake_minimum_required(VERSION 3.20)
project(ProjectLazarus VERSION 0.1.0 LANGUAGES C CXX ASM_MASM)

if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "Project Lazarus targets 64-bit Windows only (the game is x64 Windows).")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# static CRT so the proxy doesn't depend on whatever VC++ redist the player has
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

if(MSVC)
  add_compile_options("$<$<COMPILE_LANGUAGE:C,CXX>:/W4;/permissive-;/utf-8>")
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

enable_testing()

add_subdirectory(third_party/minhook)
add_subdirectory(src/core)
add_subdirectory(src/proxy)
add_subdirectory(mods/graphics-settings)
add_subdirectory(mods/fps-counter)
add_subdirectory(mods/dev-console)
add_subdirectory(mods/skip-intro)
add_subdirectory(mods/mouse-sensitivity)
add_subdirectory(mods/key-rebind)
add_subdirectory(examples/hello-mod)
add_subdirectory(tests)

# build/dist = exactly what a player gets: version.dll + Lazarus/ + installer.
# the graphics-settings target already drops its files in there.
set(DIST ${CMAKE_BINARY_DIR}/dist)
file(WRITE ${DIST}/VERSION.txt "${PROJECT_VERSION}\n")
add_custom_target(dist ALL
  COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:lazarus_proxy> ${DIST}/version.dll
  COMMAND $<TARGET_FILE:lazarus_write_default_config> ${DIST}/Lazarus/lazarus.ini
  COMMAND ${CMAKE_COMMAND} -E make_directory ${DIST}/Lazarus/mods
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${PROJECT_SOURCE_DIR}/installer ${DIST}
  COMMENT "Assembling build/dist")
add_dependencies(dist lazarus_proxy graphics_settings fps_counter dev_console skip_intro mouse_sensitivity key_rebind lazarus_write_default_config)
