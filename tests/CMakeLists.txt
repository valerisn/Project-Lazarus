add_executable(lazarus_tests
  test_main.cpp
  test_ini.cpp
  test_log.cpp
  test_manifest.cpp
  test_config.cpp
  test_plan.cpp
  test_state.cpp
  test_command.cpp
  test_rebind_parse.cpp
  test_cheat_policy.cpp
  ../mods/key-rebind/rebind_parse.cpp
  test_gfx_settings.cpp
  ../mods/graphics-settings/settings.cpp
)
target_link_libraries(lazarus_tests PRIVATE lazarus_core)
add_test(NAME core_unit_tests COMMAND lazarus_tests)
add_subdirectory(e2e)

# installer tests need Pester, which ships with Windows PowerShell 5.1
add_test(NAME installer_tests
  COMMAND powershell -NoProfile -ExecutionPolicy Bypass -Command
    "Import-Module Pester -MaximumVersion 4.99; Invoke-Pester -Script '${CMAKE_CURRENT_SOURCE_DIR}/installer/Install-Lazarus.Tests.ps1' -EnableExit")

# python tooling tests, skipped if there's no python on the box
find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
  add_test(NAME tools_tests COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tools/test_il2cpp_meta.py)
endif()
