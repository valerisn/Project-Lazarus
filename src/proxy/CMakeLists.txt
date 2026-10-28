# output has to be version.dll, but the target isn't called that so nothing
# links our import lib by accident thinking it's the windows one
add_library(lazarus_proxy SHARED
  dllmain.cpp
  iat.cpp
  runtime.cpp
  version_forward.asm
  version.def
)
set_target_properties(lazarus_proxy PROPERTIES OUTPUT_NAME version)
target_include_directories(lazarus_proxy PRIVATE ${PROJECT_SOURCE_DIR}/src ${PROJECT_SOURCE_DIR}/include)
target_compile_definitions(lazarus_proxy PRIVATE LAZARUS_VERSION="${PROJECT_VERSION}")
target_link_libraries(lazarus_proxy PRIVATE lazarus_core minhook)
