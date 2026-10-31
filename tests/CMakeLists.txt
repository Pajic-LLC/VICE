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

add_executable(vice_compute_rhi_smoke_tests compute_rhi_smoke_tests.cpp)
target_link_libraries(vice_compute_rhi_smoke_tests PRIVATE VICE::Render)
target_compile_features(vice_compute_rhi_smoke_tests PRIVATE cxx_std_23)
vice_enable_warnings(vice_compute_rhi_smoke_tests)
add_test(NAME vice_compute_rhi_smoke_tests COMMAND vice_compute_rhi_smoke_tests)

add_executable(
  vice_gpu_material_selection_smoke_tests
  gpu_material_selection_smoke_tests.cpp
)
target_link_libraries(vice_gpu_material_selection_smoke_tests PRIVATE VICE::Render)
target_compile_features(vice_gpu_material_selection_smoke_tests PRIVATE cxx_std_23)
vice_enable_warnings(vice_gpu_material_selection_smoke_tests)
add_test(
  NAME vice_gpu_material_selection_smoke_tests
  COMMAND vice_gpu_material_selection_smoke_tests
)

add_executable(vice_indexed_indirect_draw_tests indexed_indirect_draw_tests.cpp)
target_link_libraries(vice_indexed_indirect_draw_tests PRIVATE VICE::Render)
target_compile_features(vice_indexed_indirect_draw_tests PRIVATE cxx_std_23)
vice_enable_warnings(vice_indexed_indirect_draw_tests)
add_test(NAME vice_indexed_indirect_draw_tests COMMAND vice_indexed_indirect_draw_tests)

add_executable(vice_depth_state_tests depth_state_tests.cpp)
target_link_libraries(vice_depth_state_tests PRIVATE VICE::Render)
target_compile_features(vice_depth_state_tests PRIVATE cxx_std_23)
vice_enable_warnings(vice_depth_state_tests)
add_test(NAME vice_depth_state_tests COMMAND vice_depth_state_tests)

add_executable(vice_depth_only_render_pass_tests depth_only_render_pass_tests.cpp)
target_link_libraries(vice_depth_only_render_pass_tests PRIVATE VICE::Render)
target_compile_features(vice_depth_only_render_pass_tests PRIVATE cxx_std_23)
vice_enable_warnings(vice_depth_only_render_pass_tests)
add_test(NAME vice_depth_only_render_pass_tests COMMAND vice_depth_only_render_pass_tests)

add_executable(vice_offscreen_color_render_pass_tests offscreen_color_render_pass_tests.cpp)
target_link_libraries(vice_offscreen_color_render_pass_tests PRIVATE VICE::Render)
target_compile_features(vice_offscreen_color_render_pass_tests PRIVATE cxx_std_23)
vice_enable_warnings(vice_offscreen_color_render_pass_tests)
add_test(NAME vice_offscreen_color_render_pass_tests COMMAND vice_offscreen_color_render_pass_tests)

add_executable(vice_texture_mip_upload_tests texture_mip_upload_tests.cpp)
target_link_libraries(vice_texture_mip_upload_tests PRIVATE VICE::Render)
target_compile_features(vice_texture_mip_upload_tests PRIVATE cxx_std_23)
vice_enable_warnings(vice_texture_mip_upload_tests)
add_test(NAME vice_texture_mip_upload_tests COMMAND vice_texture_mip_upload_tests)

add_executable(vice_hdr_tone_map_tests hdr_tone_map_tests.cpp)
target_link_libraries(vice_hdr_tone_map_tests PRIVATE VICE::Render)
target_compile_features(vice_hdr_tone_map_tests PRIVATE cxx_std_23)
vice_enable_warnings(vice_hdr_tone_map_tests)
add_test(NAME vice_hdr_tone_map_tests COMMAND vice_hdr_tone_map_tests)

add_executable(vice_runtime_post_process_tests runtime_post_process_tests.cpp)
target_link_libraries(vice_runtime_post_process_tests PRIVATE VICE::Runtime)
target_compile_features(vice_runtime_post_process_tests PRIVATE cxx_std_23)
vice_enable_warnings(vice_runtime_post_process_tests)
add_test(NAME vice_runtime_post_process_tests COMMAND vice_runtime_post_process_tests)

add_executable(
  vice_runtime_scene_ray_tracing_telemetry_tests
  runtime_scene_ray_tracing_telemetry_tests.cpp
)
target_link_libraries(
  vice_runtime_scene_ray_tracing_telemetry_tests
  PRIVATE VICE::Runtime
)
target_compile_features(vice_runtime_scene_ray_tracing_telemetry_tests PRIVATE cxx_std_23)
vice_enable_warnings(vice_runtime_scene_ray_tracing_telemetry_tests)
add_test(
  NAME vice_runtime_scene_ray_tracing_telemetry_tests
  COMMAND vice_runtime_scene_ray_tracing_telemetry_tests
)

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

  add_executable(vice_d3d12_compute_pipeline_tests d3d12_compute_pipeline_tests.cpp)
  target_include_directories(
    vice_d3d12_compute_pipeline_tests
    PRIVATE
      ${CMAKE_SOURCE_DIR}/engine/render/src/d3d12
  )
  target_link_libraries(
    vice_d3d12_compute_pipeline_tests
    PRIVATE
      VICE::Render
      d3d12
      d3dcompiler
      dxgi
  )
  target_compile_features(vice_d3d12_compute_pipeline_tests PRIVATE cxx_std_23)
  vice_enable_warnings(vice_d3d12_compute_pipeline_tests)
  add_test(NAME vice_d3d12_compute_pipeline_tests COMMAND vice_d3d12_compute_pipeline_tests)
endif()

if(APPLE)
  enable_language(OBJCXX)
  find_library(METAL_FRAMEWORK_TEST Metal REQUIRED)
  add_executable(vice_metal_compute_pipeline_tests metal_compute_pipeline_tests.mm)
  target_include_directories(
    vice_metal_compute_pipeline_tests
    PRIVATE
      ${CMAKE_SOURCE_DIR}/engine/render/src/metal
  )
  target_link_libraries(
    vice_metal_compute_pipeline_tests
    PRIVATE
      VICE::Render
      ${METAL_FRAMEWORK_TEST}
  )
  target_compile_features(vice_metal_compute_pipeline_tests PRIVATE cxx_std_23)
  set_target_properties(
    vice_metal_compute_pipeline_tests
    PROPERTIES
      OBJCXX_STANDARD 23
      OBJCXX_STANDARD_REQUIRED YES
      OBJCXX_EXTENSIONS NO
  )
  set_source_files_properties(
    metal_compute_pipeline_tests.mm
    PROPERTIES COMPILE_OPTIONS "-fobjc-arc"
  )
  vice_enable_warnings(vice_metal_compute_pipeline_tests)
  add_test(NAME vice_metal_compute_pipeline_tests COMMAND vice_metal_compute_pipeline_tests)
endif()

add_executable(vice_world_tests world_tests.cpp)
target_link_libraries(vice_world_tests PRIVATE VICE::World)
target_compile_features(vice_world_tests PRIVATE cxx_std_23)
vice_enable_warnings(vice_world_tests)
add_test(NAME vice_world_tests COMMAND vice_world_tests)

add_executable(vice_reflection_probe_tests reflection_probe_tests.cpp)
target_link_libraries(vice_reflection_probe_tests PRIVATE VICE::Runtime VICE::World)
target_compile_features(vice_reflection_probe_tests PRIVATE cxx_std_23)
vice_enable_warnings(vice_reflection_probe_tests)
add_test(NAME vice_reflection_probe_tests COMMAND vice_reflection_probe_tests)

add_executable(vice_runtime_atmosphere_tests runtime_atmosphere_tests.cpp)
target_link_libraries(vice_runtime_atmosphere_tests PRIVATE VICE::Runtime VICE::World)
target_compile_features(vice_runtime_atmosphere_tests PRIVATE cxx_std_23)
vice_enable_warnings(vice_runtime_atmosphere_tests)
add_test(NAME vice_runtime_atmosphere_tests COMMAND vice_runtime_atmosphere_tests)

add_executable(vice_runtime_point_shadow_tests runtime_point_shadow_tests.cpp)
target_link_libraries(vice_runtime_point_shadow_tests PRIVATE VICE::Runtime VICE::World)
target_compile_features(vice_runtime_point_shadow_tests PRIVATE cxx_std_23)
vice_enable_warnings(vice_runtime_point_shadow_tests)
add_test(NAME vice_runtime_point_shadow_tests COMMAND vice_runtime_point_shadow_tests)

add_executable(vice_world_light_tests world_light_tests.cpp)
target_link_libraries(vice_world_light_tests PRIVATE VICE::World)
target_compile_features(vice_world_light_tests PRIVATE cxx_std_23)
vice_enable_warnings(vice_world_light_tests)
add_test(NAME vice_world_light_tests COMMAND vice_world_light_tests)

add_executable(vice_world_primitives_tests world_primitives_tests.cpp)
target_link_libraries(vice_world_primitives_tests PRIVATE VICE::World)
target_compile_features(vice_world_primitives_tests PRIVATE cxx_std_23)
vice_enable_warnings(vice_world_primitives_tests)
add_test(NAME vice_world_primitives_tests COMMAND vice_world_primitives_tests)

add_executable(vice_world_region_bounds_tests world_region_bounds_tests.cpp)
target_link_libraries(vice_world_region_bounds_tests PRIVATE VICE::World)
target_compile_features(vice_world_region_bounds_tests PRIVATE cxx_std_23)
vice_enable_warnings(vice_world_region_bounds_tests)
add_test(NAME vice_world_region_bounds_tests COMMAND vice_world_region_bounds_tests)

add_executable(vice_simulation_tests simulation_tests.cpp)
target_link_libraries(vice_simulation_tests PRIVATE VICE::Simulation)
target_compile_features(vice_simulation_tests PRIVATE cxx_std_23)
vice_enable_warnings(vice_simulation_tests)
add_test(NAME vice_simulation_tests COMMAND vice_simulation_tests)

if(TARGET vice_rigid_body_world AND TARGET vice_world_cooker_lib)
  add_executable(vice_authored_simulation_tests authored_simulation_tests.cpp)
  target_link_libraries(vice_authored_simulation_tests PRIVATE VICE::RigidBodyWorld VICE::WorldCooker VICE::Jobs)
  if(TARGET vice_editor_preview)
    target_link_libraries(vice_authored_simulation_tests PRIVATE VICE::EditorPreview VICE::EditorState)
    target_compile_definitions(vice_authored_simulation_tests PRIVATE VICE_TEST_EDITOR_PREVIEW=1)
  endif()
  target_compile_features(vice_authored_simulation_tests PRIVATE cxx_std_23)
  vice_enable_warnings(vice_authored_simulation_tests)
  add_test(NAME vice_authored_simulation_tests COMMAND vice_authored_simulation_tests)
endif()

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

  add_executable(vice_rigid_body_debug_tests rigid_body_debug_tests.cpp)
  target_link_libraries(vice_rigid_body_debug_tests PRIVATE VICE::RigidBody)
  target_compile_features(vice_rigid_body_debug_tests PRIVATE cxx_std_23)
  vice_enable_warnings(vice_rigid_body_debug_tests)
  add_test(NAME vice_rigid_body_debug_tests COMMAND vice_rigid_body_debug_tests)

  add_executable(vice_rigid_body_ccd_tests rigid_body_ccd_tests.cpp)
  target_link_libraries(vice_rigid_body_ccd_tests PRIVATE VICE::RigidBody)
  target_compile_features(vice_rigid_body_ccd_tests PRIVATE cxx_std_23)
  vice_enable_warnings(vice_rigid_body_ccd_tests)
  add_test(NAME vice_rigid_body_ccd_tests COMMAND vice_rigid_body_ccd_tests)

  add_executable(vice_rigid_body_character_tests rigid_body_character_tests.cpp)
  target_link_libraries(vice_rigid_body_character_tests PRIVATE VICE::RigidBody)
  target_compile_features(vice_rigid_body_character_tests PRIVATE cxx_std_23)
  vice_enable_warnings(vice_rigid_body_character_tests)
  add_test(NAME vice_rigid_body_character_tests COMMAND vice_rigid_body_character_tests)

  add_executable(vice_rigid_body_character_world_tests rigid_body_character_world_tests.cpp)
  target_link_libraries(vice_rigid_body_character_world_tests PRIVATE VICE::RigidBodyWorld)
  target_compile_features(vice_rigid_body_character_world_tests PRIVATE cxx_std_23)
  vice_enable_warnings(vice_rigid_body_character_world_tests)
  add_test(NAME vice_rigid_body_character_world_tests COMMAND vice_rigid_body_character_world_tests)

  add_executable(
    vice_rigid_body_character_orchestration_tests
    rigid_body_character_orchestration_tests.cpp
  )
  target_link_libraries(
    vice_rigid_body_character_orchestration_tests
    PRIVATE VICE::RigidBodyWorld
  )
  target_compile_features(vice_rigid_body_character_orchestration_tests PRIVATE cxx_std_23)
  vice_enable_warnings(vice_rigid_body_character_orchestration_tests)
  add_test(
    NAME vice_rigid_body_character_orchestration_tests
    COMMAND vice_rigid_body_character_orchestration_tests
  )

  add_executable(vice_rigid_body_jobs_tests rigid_body_jobs_tests.cpp)
  target_link_libraries(vice_rigid_body_jobs_tests PRIVATE VICE::RigidBody)
  target_compile_features(vice_rigid_body_jobs_tests PRIVATE cxx_std_23)
  vice_enable_warnings(vice_rigid_body_jobs_tests)
  add_test(NAME vice_rigid_body_jobs_tests COMMAND vice_rigid_body_jobs_tests)

  add_executable(vice_rigid_body_constraint_tests rigid_body_constraint_tests.cpp)
  target_link_libraries(vice_rigid_body_constraint_tests PRIVATE VICE::RigidBody)
  target_compile_features(vice_rigid_body_constraint_tests PRIVATE cxx_std_23)
  vice_enable_warnings(vice_rigid_body_constraint_tests)
  add_test(NAME vice_rigid_body_constraint_tests COMMAND vice_rigid_body_constraint_tests)

  add_executable(vice_rigid_body_contact_tests rigid_body_contact_tests.cpp)
  target_link_libraries(vice_rigid_body_contact_tests PRIVATE VICE::RigidBody)
  target_compile_features(vice_rigid_body_contact_tests PRIVATE cxx_std_23)
  vice_enable_warnings(vice_rigid_body_contact_tests)
  add_test(NAME vice_rigid_body_contact_tests COMMAND vice_rigid_body_contact_tests)

  add_executable(vice_rigid_body_query_tests rigid_body_query_tests.cpp)
  target_link_libraries(vice_rigid_body_query_tests PRIVATE VICE::RigidBody)
  target_compile_features(vice_rigid_body_query_tests PRIVATE cxx_std_23)
  vice_enable_warnings(vice_rigid_body_query_tests)
  add_test(NAME vice_rigid_body_query_tests COMMAND vice_rigid_body_query_tests)

  add_executable(vice_rigid_body_geometry_tests rigid_body_geometry_tests.cpp)
  target_link_libraries(vice_rigid_body_geometry_tests PRIVATE VICE::RigidBody)
  target_compile_features(vice_rigid_body_geometry_tests PRIVATE cxx_std_23)
  vice_enable_warnings(vice_rigid_body_geometry_tests)
  add_test(NAME vice_rigid_body_geometry_tests COMMAND vice_rigid_body_geometry_tests)

  add_executable(vice_rigid_body_heightfield_tests rigid_body_heightfield_tests.cpp)
  target_link_libraries(vice_rigid_body_heightfield_tests PRIVATE VICE::RigidBody)
  target_compile_features(vice_rigid_body_heightfield_tests PRIVATE cxx_std_23)
  vice_enable_warnings(vice_rigid_body_heightfield_tests)
  add_test(NAME vice_rigid_body_heightfield_tests COMMAND vice_rigid_body_heightfield_tests)

  add_executable(vice_rigid_body_world_tests rigid_body_world_tests.cpp)
  target_link_libraries(vice_rigid_body_world_tests PRIVATE VICE::RigidBodyWorld)
  target_compile_features(vice_rigid_body_world_tests PRIVATE cxx_std_23)
  vice_enable_warnings(vice_rigid_body_world_tests)
  add_test(NAME vice_rigid_body_world_tests COMMAND vice_rigid_body_world_tests)
endif()

if(VICE_BUILD_TOOLS)
  add_executable(vice_environment_asset_tests environment_asset_tests.cpp)
  target_link_libraries(vice_environment_asset_tests PRIVATE VICE::EnvironmentCooker VICE::Render)
  target_compile_features(vice_environment_asset_tests PRIVATE cxx_std_23)
  vice_enable_warnings(vice_environment_asset_tests)
  add_test(NAME vice_environment_asset_tests COMMAND vice_environment_asset_tests)

  add_executable(vice_environment_hdr_source_tests environment_hdr_source_tests.cpp)
  target_link_libraries(vice_environment_hdr_source_tests PRIVATE VICE::EnvironmentCooker VICE::Render)
  target_compile_features(vice_environment_hdr_source_tests PRIVATE cxx_std_23)
  vice_enable_warnings(vice_environment_hdr_source_tests)
  add_test(NAME vice_environment_hdr_source_tests COMMAND vice_environment_hdr_source_tests)

  add_executable(vice_shader_cooker_tests shader_cooker_tests.cpp)
  target_link_libraries(vice_shader_cooker_tests PRIVATE VICE::ShaderCooker)
  target_compile_features(vice_shader_cooker_tests PRIVATE cxx_std_23)
  vice_enable_warnings(vice_shader_cooker_tests)
  add_test(NAME vice_shader_cooker_tests COMMAND vice_shader_cooker_tests)

  add_executable(vice_mesh_asset_tests mesh_asset_tests.cpp)
  target_link_libraries(vice_mesh_asset_tests PRIVATE VICE::MeshCooker VICE::Render)
  target_compile_features(vice_mesh_asset_tests PRIVATE cxx_std_23)
  vice_enable_warnings(vice_mesh_asset_tests)
  add_test(NAME vice_mesh_asset_tests COMMAND vice_mesh_asset_tests)

  add_executable(vice_mesh_asset_v5_tests mesh_asset_v5_tests.cpp)
  target_link_libraries(vice_mesh_asset_v5_tests PRIVATE VICE::MeshCooker VICE::Render)
  target_compile_features(vice_mesh_asset_v5_tests PRIVATE cxx_std_23)
  vice_enable_warnings(vice_mesh_asset_v5_tests)
  add_test(NAME vice_mesh_asset_v5_tests COMMAND vice_mesh_asset_v5_tests)

  if(VICE_ENABLE_JOLT_RIGID_BODY)
    add_executable(vice_collision_cooker_tests collision_cooker_tests.cpp)
    target_link_libraries(vice_collision_cooker_tests PRIVATE VICE::CollisionCooker)
    target_compile_features(vice_collision_cooker_tests PRIVATE cxx_std_23)
    vice_enable_warnings(vice_collision_cooker_tests)
    add_test(NAME vice_collision_cooker_tests COMMAND vice_collision_cooker_tests)

    add_executable(
      vice_collision_cooker_heightfield_guard_tests
      collision_cooker_heightfield_guard_tests.cpp
    )
    target_link_libraries(
      vice_collision_cooker_heightfield_guard_tests
      PRIVATE VICE::CollisionCooker
    )
    target_compile_features(vice_collision_cooker_heightfield_guard_tests PRIVATE cxx_std_23)
    vice_enable_warnings(vice_collision_cooker_heightfield_guard_tests)
    add_test(
      NAME vice_collision_cooker_heightfield_guard_tests
      COMMAND vice_collision_cooker_heightfield_guard_tests
    )

    add_executable(vice_collision_import_tests collision_import_tests.cpp)
    target_link_libraries(vice_collision_import_tests PRIVATE VICE::CollisionImport)
    target_compile_features(vice_collision_import_tests PRIVATE cxx_std_23)
    vice_enable_warnings(vice_collision_import_tests)
    add_test(NAME vice_collision_import_tests COMMAND vice_collision_import_tests)
  endif()
endif()

add_executable(vice_runtime_fallback_lighting_tests runtime_fallback_lighting_tests.cpp)
target_link_libraries(vice_runtime_fallback_lighting_tests PRIVATE VICE::Render VICE::Math)
target_compile_features(vice_runtime_fallback_lighting_tests PRIVATE cxx_std_23)
vice_enable_warnings(vice_runtime_fallback_lighting_tests)
add_test(NAME vice_runtime_fallback_lighting_tests COMMAND vice_runtime_fallback_lighting_tests)

add_executable(vice_animation_mix_tests animation_mix_tests.cpp)
target_link_libraries(vice_animation_mix_tests PRIVATE VICE::Presentation VICE::PresentationCooker VICE::MeshCooker VICE::WorldCooker)
target_compile_definitions(vice_animation_mix_tests PRIVATE VICE_PRESENTATION_EXAMPLE="${PROJECT_SOURCE_DIR}/examples/presentation-playground")
target_compile_features(vice_animation_mix_tests PRIVATE cxx_std_23)
vice_enable_warnings(vice_animation_mix_tests)
add_test(NAME vice_animation_mix_tests COMMAND vice_animation_mix_tests)

if(_vice_runtime_ray_shadow_target)
  foreach(_stage IN ITEMS vertex fragment)
    if(_stage STREQUAL "vertex")
      set(_entry depth_test_vs)
    else()
      set(_entry depth_test_fs)
    endif()
    set(_out "${CMAKE_BINARY_DIR}/cooked/vice_material_depth_test_${_stage}")
    vice_add_cooked_shader(NAME vice_material_depth_test_${_stage}_shader
      INPUT "${CMAKE_CURRENT_SOURCE_DIR}/material_depth_fixture.hlsl" OUTPUT "${_out}"
      ENTRY "${_entry}" STAGE "${_stage}" TARGET "${_vice_runtime_ray_shadow_target}"
      SHADER_MODEL 6.0 DXC "${_vice_runtime_ray_shadow_dxc}"
      METAL_SHADER_CONVERTER "${_vice_runtime_ray_shadow_metal_converter}")
  endforeach()
  vice_add_cooked_shader(NAME vice_model_layout_test_vertex_shader
    INPUT "${PROJECT_SOURCE_DIR}/assets/shaders/vice_gpu_pbr_surface.hlsl"
    OUTPUT "${CMAKE_BINARY_DIR}/cooked/vice_model_layout_test_vertex"
    ENTRY vice_gpu_pbr_vs STAGE vertex TARGET "${_vice_runtime_ray_shadow_target}"
    SHADER_MODEL 6.0 DXC "${_vice_runtime_ray_shadow_dxc}"
    METAL_SHADER_CONVERTER "${_vice_runtime_ray_shadow_metal_converter}")
  add_executable(vice_runtime_material_depth_tests runtime_material_depth_tests.cpp)
  target_link_libraries(vice_runtime_material_depth_tests PRIVATE VICE::Runtime)
  target_compile_features(vice_runtime_material_depth_tests PRIVATE cxx_std_23)
  target_compile_definitions(vice_runtime_material_depth_tests PRIVATE
    VICE_MODEL_TEST_VS="${CMAKE_BINARY_DIR}/cooked/vice_model_layout_test_vertex/shader.vice.json"
    VICE_DEPTH_TEST_VS="${CMAKE_BINARY_DIR}/cooked/vice_material_depth_test_vertex/shader.vice.json"
    VICE_DEPTH_TEST_FS="${CMAKE_BINARY_DIR}/cooked/vice_material_depth_test_fragment/shader.vice.json")
  add_dependencies(vice_runtime_material_depth_tests vice_material_depth_test_vertex_shader vice_material_depth_test_fragment_shader vice_model_layout_test_vertex_shader)
  vice_enable_warnings(vice_runtime_material_depth_tests)
  add_test(NAME vice_runtime_material_depth_tests COMMAND vice_runtime_material_depth_tests)
endif()
