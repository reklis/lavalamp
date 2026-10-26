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
# INTERFACE for now because there are no core sources yet -- an empty STATIC
# library is a configure error. This becomes STATIC once src/color.cpp exists.
add_library(lavalamp_core INTERFACE)
target_include_directories(lavalamp_core INTERFACE src)

add_executable(lavalamp src/main.cpp)
target_link_libraries(lavalamp PRIVATE lavalamp_core)

enable_testing()

add_executable(lavalamp_tests
  tests/main.cpp
  tests/test_harness.cpp
  tests/test_smoke.cpp
)
target_link_libraries(lavalamp_tests PRIVATE lavalamp_core)
target_include_directories(lavalamp_tests PRIVATE tests)

add_test(NAME unit COMMAND lavalamp_tests)
