add_executable(vice_project_tests project_tests.cpp)
target_link_libraries(vice_project_tests PRIVATE VICE::Project)
target_compile_features(vice_project_tests PRIVATE cxx_std_23)
vice_enable_warnings(vice_project_tests)
add_test(NAME vice_project_tests COMMAND vice_project_tests)

add_executable(vice_project_workspace_tests project_workspace_tests.cpp)
target_link_libraries(vice_project_workspace_tests PRIVATE VICE::Project)
target_compile_features(vice_project_workspace_tests PRIVATE cxx_std_23)
vice_enable_warnings(vice_project_workspace_tests)
add_test(NAME vice_project_workspace_tests COMMAND vice_project_workspace_tests)

if(TARGET vice_mesh_cooker_lib)
  add_executable(vice_project_mesh_cook_tests project_mesh_cook_tests.cpp)
  target_link_libraries(vice_project_mesh_cook_tests PRIVATE VICE::MeshCooker)
  target_compile_features(vice_project_mesh_cook_tests PRIVATE cxx_std_23)
  vice_enable_warnings(vice_project_mesh_cook_tests)
  add_test(NAME vice_project_mesh_cook_tests COMMAND vice_project_mesh_cook_tests)
endif()

if(TARGET vice_texture_cooker_lib AND TARGET vice_environment_cooker_lib)
  add_executable(
    vice_project_texture_environment_cook_tests
    project_texture_environment_cook_tests.cpp
  )
  target_link_libraries(
    vice_project_texture_environment_cook_tests
    PRIVATE
      VICE::TextureCooker
      VICE::EnvironmentCooker
  )
  target_compile_features(vice_project_texture_environment_cook_tests PRIVATE cxx_std_23)
  vice_enable_warnings(vice_project_texture_environment_cook_tests)
  add_test(
    NAME vice_project_texture_environment_cook_tests
    COMMAND vice_project_texture_environment_cook_tests
  )
endif()

if(TARGET vice_render_material_cooker_lib)
  add_executable(
    vice_project_render_material_cook_tests
    project_render_material_cook_tests.cpp
  )
  target_link_libraries(
    vice_project_render_material_cook_tests
    PRIVATE
      VICE::RenderMaterialCooker
  )
  target_compile_features(vice_project_render_material_cook_tests PRIVATE cxx_std_23)
  vice_enable_warnings(vice_project_render_material_cook_tests)
  add_test(
    NAME vice_project_render_material_cook_tests
    COMMAND vice_project_render_material_cook_tests
  )
endif()

if(TARGET vice_project_workflow)
  add_executable(vice_project_workflow_tests project_workflow_tests.cpp)
  target_link_libraries(
    vice_project_workflow_tests
    PRIVATE
      VICE::ProjectWorkflow
      VICE::WorldCooker
  )
  target_compile_features(vice_project_workflow_tests PRIVATE cxx_std_23)
  vice_enable_warnings(vice_project_workflow_tests)
  add_test(NAME vice_project_workflow_tests COMMAND vice_project_workflow_tests)
endif()

if(TARGET vice_dev_cli)
  add_executable(vice_dev_cli_tests dev_cli_tests.cpp)
  target_link_libraries(vice_dev_cli_tests PRIVATE VICE::DevCLI)
  target_compile_features(vice_dev_cli_tests PRIVATE cxx_std_23)
  vice_enable_warnings(vice_dev_cli_tests)
  add_test(NAME vice_dev_cli_tests COMMAND vice_dev_cli_tests)
endif()
