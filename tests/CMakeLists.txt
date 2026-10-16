add_executable(vice_core_tests core_tests.cpp)
target_link_libraries(vice_core_tests PRIVATE VICE::Core)
target_compile_features(vice_core_tests PRIVATE cxx_std_23)
vice_enable_warnings(vice_core_tests)
add_test(NAME vice_core_tests COMMAND vice_core_tests)

add_executable(vice_math_tests math_tests.cpp)
target_link_libraries(vice_math_tests PRIVATE VICE::Math)
target_compile_features(vice_math_tests PRIVATE cxx_std_23)
vice_enable_warnings(vice_math_tests)
add_test(NAME vice_math_tests COMMAND vice_math_tests)

add_executable(vice_jobs_tests jobs_tests.cpp)
target_link_libraries(vice_jobs_tests PRIVATE VICE::Jobs)
target_compile_features(vice_jobs_tests PRIVATE cxx_std_23)
vice_enable_warnings(vice_jobs_tests)
add_test(NAME vice_jobs_tests COMMAND vice_jobs_tests)

add_executable(vice_platform_tests platform_tests.cpp)
target_link_libraries(vice_platform_tests PRIVATE VICE::Platform)
target_compile_features(vice_platform_tests PRIVATE cxx_std_23)
vice_enable_warnings(vice_platform_tests)
add_test(NAME vice_platform_tests COMMAND vice_platform_tests)

add_executable(vice_render_tests render_tests.cpp)
target_link_libraries(vice_render_tests PRIVATE VICE::Render)
target_compile_features(vice_render_tests PRIVATE cxx_std_23)
vice_enable_warnings(vice_render_tests)
add_test(NAME vice_render_tests COMMAND vice_render_tests)

add_executable(vice_depth_state_tests depth_state_tests.cpp)
target_link_libraries(vice_depth_state_tests PRIVATE VICE::Render)
target_compile_features(vice_depth_state_tests PRIVATE cxx_std_23)
vice_enable_warnings(vice_depth_state_tests)
add_test(NAME vice_depth_state_tests COMMAND vice_depth_state_tests)

add_executable(vice_mesh_input_tests mesh_input_tests.cpp)
target_link_libraries(vice_mesh_input_tests PRIVATE VICE::Render)
target_compile_features(vice_mesh_input_tests PRIVATE cxx_std_23)
vice_enable_warnings(vice_mesh_input_tests)
add_test(NAME vice_mesh_input_tests COMMAND vice_mesh_input_tests)

add_executable(vice_shader_asset_tests shader_asset_tests.cpp)
target_link_libraries(vice_shader_asset_tests PRIVATE VICE::Render)
target_compile_features(vice_shader_asset_tests PRIVATE cxx_std_23)
vice_enable_warnings(vice_shader_asset_tests)
add_test(NAME vice_shader_asset_tests COMMAND vice_shader_asset_tests)

add_executable(vice_pipeline_layout_tests pipeline_layout_tests.cpp)
target_link_libraries(vice_pipeline_layout_tests PRIVATE VICE::Render)
target_compile_features(vice_pipeline_layout_tests PRIVATE cxx_std_23)
vice_enable_warnings(vice_pipeline_layout_tests)
add_test(NAME vice_pipeline_layout_tests COMMAND vice_pipeline_layout_tests)

if(WIN32)
  add_executable(vice_d3d12_root_signature_tests d3d12_root_signature_tests.cpp)
  target_link_libraries(vice_d3d12_root_signature_tests PRIVATE VICE::Render d3d12)
  target_compile_features(vice_d3d12_root_signature_tests PRIVATE cxx_std_23)
  vice_enable_warnings(vice_d3d12_root_signature_tests)
  add_test(NAME vice_d3d12_root_signature_tests COMMAND vice_d3d12_root_signature_tests)
endif()

add_executable(vice_world_tests world_tests.cpp)
target_link_libraries(vice_world_tests PRIVATE VICE::World)
target_compile_features(vice_world_tests PRIVATE cxx_std_23)
vice_enable_warnings(vice_world_tests)
add_test(NAME vice_world_tests COMMAND vice_world_tests)

add_executable(vice_simulation_tests simulation_tests.cpp)
target_link_libraries(vice_simulation_tests PRIVATE VICE::Simulation)
target_compile_features(vice_simulation_tests PRIVATE cxx_std_23)
vice_enable_warnings(vice_simulation_tests)
add_test(NAME vice_simulation_tests COMMAND vice_simulation_tests)

add_executable(vice_entity_binding_tests entity_binding_tests.cpp)
target_link_libraries(vice_entity_binding_tests PRIVATE VICE::SimulationWorld)
target_compile_features(vice_entity_binding_tests PRIVATE cxx_std_23)
vice_enable_warnings(vice_entity_binding_tests)
add_test(NAME vice_entity_binding_tests COMMAND vice_entity_binding_tests)

if(VICE_ENABLE_JOLT_RIGID_BODY)
  add_executable(vice_rigid_body_tests rigid_body_tests.cpp)
  target_link_libraries(
    vice_rigid_body_tests
    PRIVATE
      VICE::RigidBody
      VICE::SimulationWorld
      VICE::World
  )
  target_compile_features(vice_rigid_body_tests PRIVATE cxx_std_23)
  vice_enable_warnings(vice_rigid_body_tests)
  add_test(NAME vice_rigid_body_tests COMMAND vice_rigid_body_tests)

  add_executable(vice_rigid_body_contact_tests rigid_body_contact_tests.cpp)
  target_link_libraries(vice_rigid_body_contact_tests PRIVATE VICE::RigidBody)
  target_compile_features(vice_rigid_body_contact_tests PRIVATE cxx_std_23)
  vice_enable_warnings(vice_rigid_body_contact_tests)
  add_test(NAME vice_rigid_body_contact_tests COMMAND vice_rigid_body_contact_tests)

  add_executable(vice_rigid_body_world_tests rigid_body_world_tests.cpp)
  target_link_libraries(vice_rigid_body_world_tests PRIVATE VICE::RigidBodyWorld)
  target_compile_features(vice_rigid_body_world_tests PRIVATE cxx_std_23)
  vice_enable_warnings(vice_rigid_body_world_tests)
  add_test(NAME vice_rigid_body_world_tests COMMAND vice_rigid_body_world_tests)
endif()

if(VICE_BUILD_TOOLS)
  add_executable(vice_shader_cooker_tests shader_cooker_tests.cpp)
  target_link_libraries(vice_shader_cooker_tests PRIVATE VICE::ShaderCooker)
  target_compile_features(vice_shader_cooker_tests PRIVATE cxx_std_23)
  vice_enable_warnings(vice_shader_cooker_tests)
  add_test(NAME vice_shader_cooker_tests COMMAND vice_shader_cooker_tests)
endif()
