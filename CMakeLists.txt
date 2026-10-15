cmake_minimum_required(VERSION 3.28)

project(
  VICE
  VERSION 0.1.0
  DESCRIPTION "Visual Interactive Computational Engine"
  LANGUAGES CXX
)

option(VICE_BUILD_TESTS "Build VICE tests" ON)
option(VICE_BUILD_LAB "Build the VICE Lab executable" ON)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(ViceCompiler)

add_subdirectory(engine/core)
add_subdirectory(engine/jobs)
add_subdirectory(engine/platform)
add_subdirectory(engine/world)
add_subdirectory(engine/render)

if(VICE_BUILD_LAB)
  add_subdirectory(apps/vice_lab)
endif()

if(VICE_BUILD_TESTS)
  include(CTest)
  enable_testing()
  add_subdirectory(tests)
endif()
