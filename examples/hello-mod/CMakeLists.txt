add_library(hello_mod SHARED hello_mod.cpp)
target_include_directories(hello_mod PRIVATE ${PROJECT_SOURCE_DIR}/include)
# not in dist, players don't need it
