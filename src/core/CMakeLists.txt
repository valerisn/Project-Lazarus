# no game stuff in here, that's what makes it testable
add_library(lazarus_core STATIC
  ini.cpp
  log.cpp
  manifest.cpp
  config.cpp
  plan.cpp
  state.cpp
  command.cpp
)
target_include_directories(lazarus_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)

add_executable(lazarus_write_default_config write_default_config.cpp)
target_link_libraries(lazarus_write_default_config PRIVATE lazarus_core)
