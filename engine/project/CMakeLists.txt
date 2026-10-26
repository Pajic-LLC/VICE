add_library(
  vice_project
  STATIC
    src/project.cpp
    src/workspace.cpp
)

add_library(VICE::Project ALIAS vice_project)

target_include_directories(
  vice_project
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_compile_features(vice_project PUBLIC cxx_std_23)
vice_enable_warnings(vice_project)
