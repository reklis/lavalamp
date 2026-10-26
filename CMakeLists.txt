cmake_minimum_required(VERSION 3.20)
project(lavalamp CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(-Wall -Wextra)

# Sources are listed explicitly rather than globbed: each task adds its own
# files here, and a glob would silently miss them until the next configure.
add_library(lavalamp_core STATIC
  src/color.cpp
  src/palette.cpp
)
target_include_directories(lavalamp_core PUBLIC src)

add_executable(lavalamp src/main.cpp)
target_link_libraries(lavalamp PRIVATE lavalamp_core)

enable_testing()

add_executable(lavalamp_tests
  tests/main.cpp
  tests/test_harness.cpp
  tests/test_smoke.cpp
  tests/test_color.cpp
  tests/test_palette.cpp
)
target_link_libraries(lavalamp_tests PRIVATE lavalamp_core)
target_include_directories(lavalamp_tests PRIVATE tests)

add_test(NAME unit COMMAND lavalamp_tests)
