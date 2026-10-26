add_library(
  vice_project_workflow
  STATIC
    src/project_workspace.cpp
)
add_library(VICE::ProjectWorkflow ALIAS vice_project_workflow)

target_include_directories(
  vice_project_workflow
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(
  vice_project_workflow
  PUBLIC
    VICE::Project
)
target_compile_features(vice_project_workflow PUBLIC cxx_std_23)
vice_enable_warnings(vice_project_workflow)
