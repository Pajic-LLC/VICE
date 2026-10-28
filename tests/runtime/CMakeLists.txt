function(vice_add_runtime_module_fixture target source)
  add_library(${target} MODULE ${source})
  target_include_directories(
    ${target}
    PRIVATE
      "${PROJECT_SOURCE_DIR}/engine/runtime/include"
  )
  target_compile_features(${target} PRIVATE cxx_std_23)
  set_target_properties(
    ${target}
    PROPERTIES
      PREFIX ""
      CXX_VISIBILITY_PRESET hidden
      VISIBILITY_INLINES_HIDDEN YES
  )
  if(APPLE)
    set_target_properties(${target} PROPERTIES SUFFIX ".dylib")
  endif()
  vice_enable_warnings(${target})
endfunction()

vice_add_runtime_module_fixture(vice_runtime_test_module test_game_module.cpp)
vice_add_runtime_module_fixture(vice_runtime_legacy_module test_legacy_game_module.cpp)
vice_add_runtime_module_fixture(vice_runtime_bad_module test_bad_game_module.cpp)
vice_add_runtime_module_fixture(vice_runtime_incompatible_module test_incompatible_game_module.cpp)

add_executable(
  vice_runtime_tests
  ../runtime_tests.cpp
)

target_link_libraries(
  vice_runtime_tests
  PRIVATE
    VICE::Runtime
)

add_dependencies(
  vice_runtime_tests
  vice_runtime_test_module
  vice_runtime_legacy_module
  vice_runtime_bad_module
  vice_runtime_incompatible_module
)

target_compile_definitions(
  vice_runtime_tests
  PRIVATE
    VICE_RUNTIME_LEGACY_MODULE="$<TARGET_FILE:vice_runtime_legacy_module>"
    VICE_RUNTIME_TEST_MODULE="$<TARGET_FILE:vice_runtime_test_module>"
    VICE_RUNTIME_BAD_MODULE="$<TARGET_FILE:vice_runtime_bad_module>"
    VICE_RUNTIME_INCOMPATIBLE_MODULE="$<TARGET_FILE:vice_runtime_incompatible_module>"
)

target_compile_features(vice_runtime_tests PRIVATE cxx_std_23)
vice_enable_warnings(vice_runtime_tests)
add_test(NAME vice_runtime_tests COMMAND vice_runtime_tests)

add_executable(
  vice_runtime_render_material_tests
  ../runtime_render_material_tests.cpp
)
target_link_libraries(
  vice_runtime_render_material_tests
  PRIVATE
    VICE::Runtime
)
target_compile_features(vice_runtime_render_material_tests PRIVATE cxx_std_23)
vice_enable_warnings(vice_runtime_render_material_tests)
add_test(NAME vice_runtime_render_material_tests COMMAND vice_runtime_render_material_tests)

add_executable(vice_runtime_light_tests ../runtime_light_tests.cpp)
target_link_libraries(vice_runtime_light_tests PRIVATE VICE::Runtime)
target_compile_features(vice_runtime_light_tests PRIVATE cxx_std_23)
vice_enable_warnings(vice_runtime_light_tests)
add_test(NAME vice_runtime_light_tests COMMAND vice_runtime_light_tests)

add_executable(
  vice_runtime_texture_tests
  ../runtime_texture_tests.cpp
)
target_link_libraries(
  vice_runtime_texture_tests
  PRIVATE
    VICE::Runtime
)
target_compile_features(vice_runtime_texture_tests PRIVATE cxx_std_23)
vice_enable_warnings(vice_runtime_texture_tests)
add_test(NAME vice_runtime_texture_tests COMMAND vice_runtime_texture_tests)

add_executable(
  vice_runtime_gpu_virtual_texture_feedback_tests
  ../runtime_gpu_virtual_texture_feedback_tests.cpp
)
target_link_libraries(
  vice_runtime_gpu_virtual_texture_feedback_tests
  PRIVATE
    VICE::Runtime
)
target_compile_features(vice_runtime_gpu_virtual_texture_feedback_tests PRIVATE cxx_std_23)
vice_enable_warnings(vice_runtime_gpu_virtual_texture_feedback_tests)
add_test(
  NAME vice_runtime_gpu_virtual_texture_feedback_tests
  COMMAND vice_runtime_gpu_virtual_texture_feedback_tests
)

add_executable(
  vice_runtime_visibility_tests
  ../runtime_visibility_tests.cpp
)
target_link_libraries(
  vice_runtime_visibility_tests
  PRIVATE
    VICE::Runtime
)
target_compile_features(vice_runtime_visibility_tests PRIVATE cxx_std_23)
vice_enable_warnings(vice_runtime_visibility_tests)
add_test(NAME vice_runtime_visibility_tests COMMAND vice_runtime_visibility_tests)

add_executable(
  vice_runtime_render_residency_tests
  ../runtime_render_residency_tests.cpp
)
target_link_libraries(
  vice_runtime_render_residency_tests
  PRIVATE
    VICE::Runtime
)
target_compile_features(vice_runtime_render_residency_tests PRIVATE cxx_std_23)
vice_enable_warnings(vice_runtime_render_residency_tests)
add_test(
  NAME vice_runtime_render_residency_tests
  COMMAND vice_runtime_render_residency_tests
)

add_executable(
  vice_runtime_gpu_draw_batch_tests
  ../runtime_gpu_draw_batch_tests.cpp
)
target_link_libraries(
  vice_runtime_gpu_draw_batch_tests
  PRIVATE
    VICE::Runtime
)
target_compile_features(vice_runtime_gpu_draw_batch_tests PRIVATE cxx_std_23)
vice_enable_warnings(vice_runtime_gpu_draw_batch_tests)
add_test(NAME vice_runtime_gpu_draw_batch_tests COMMAND vice_runtime_gpu_draw_batch_tests)

add_executable(
  vice_runtime_gpu_identity_visibility_tests
  ../runtime_gpu_identity_visibility_tests.cpp
)
target_link_libraries(
  vice_runtime_gpu_identity_visibility_tests
  PRIVATE
    VICE::Runtime
)
target_compile_features(vice_runtime_gpu_identity_visibility_tests PRIVATE cxx_std_23)
vice_enable_warnings(vice_runtime_gpu_identity_visibility_tests)
add_test(
  NAME vice_runtime_gpu_identity_visibility_tests
  COMMAND vice_runtime_gpu_identity_visibility_tests
)

add_executable(
  vice_runtime_gpu_default_batch_tests
  ../runtime_gpu_default_batch_tests.cpp
)
target_link_libraries(
  vice_runtime_gpu_default_batch_tests
  PRIVATE
    VICE::Runtime
)
target_compile_features(vice_runtime_gpu_default_batch_tests PRIVATE cxx_std_23)
vice_enable_warnings(vice_runtime_gpu_default_batch_tests)
add_test(NAME vice_runtime_gpu_default_batch_tests COMMAND vice_runtime_gpu_default_batch_tests)

add_executable(
  vice_runtime_gpu_material_table_tests
  ../runtime_gpu_material_table_tests.cpp
)
target_link_libraries(
  vice_runtime_gpu_material_table_tests
  PRIVATE
    VICE::Runtime
)
target_compile_features(vice_runtime_gpu_material_table_tests PRIVATE cxx_std_23)
vice_enable_warnings(vice_runtime_gpu_material_table_tests)
add_test(NAME vice_runtime_gpu_material_table_tests COMMAND vice_runtime_gpu_material_table_tests)

add_executable(
  vice_runtime_gpu_meshlet_table_tests
  ../runtime_gpu_meshlet_table_tests.cpp
)
target_link_libraries(
  vice_runtime_gpu_meshlet_table_tests
  PRIVATE
    VICE::Runtime
)
target_compile_features(vice_runtime_gpu_meshlet_table_tests PRIVATE cxx_std_23)
vice_enable_warnings(vice_runtime_gpu_meshlet_table_tests)
add_test(NAME vice_runtime_gpu_meshlet_table_tests COMMAND vice_runtime_gpu_meshlet_table_tests)

add_executable(
  vice_runtime_gpu_cluster_visibility_tests
  ../runtime_gpu_cluster_visibility_tests.cpp
)
target_link_libraries(
  vice_runtime_gpu_cluster_visibility_tests
  PRIVATE
    VICE::Runtime
)
target_compile_features(vice_runtime_gpu_cluster_visibility_tests PRIVATE cxx_std_23)
vice_enable_warnings(vice_runtime_gpu_cluster_visibility_tests)
add_test(
  NAME vice_runtime_gpu_cluster_visibility_tests
  COMMAND vice_runtime_gpu_cluster_visibility_tests
)

add_executable(
  vice_runtime_gpu_virtual_geometry_tests
  ../runtime_gpu_virtual_geometry_tests.cpp
)
target_link_libraries(
  vice_runtime_gpu_virtual_geometry_tests
  PRIVATE
    VICE::Runtime
)
target_compile_features(vice_runtime_gpu_virtual_geometry_tests PRIVATE cxx_std_23)
vice_enable_warnings(vice_runtime_gpu_virtual_geometry_tests)
add_test(
  NAME vice_runtime_gpu_virtual_geometry_tests
  COMMAND vice_runtime_gpu_virtual_geometry_tests
)

add_executable(
  vice_runtime_gpu_cluster_instance_remap_tests
  ../runtime_gpu_cluster_instance_remap_tests.cpp
)
target_link_libraries(
  vice_runtime_gpu_cluster_instance_remap_tests
  PRIVATE
    VICE::Runtime
)
target_compile_features(vice_runtime_gpu_cluster_instance_remap_tests PRIVATE cxx_std_23)
vice_enable_warnings(vice_runtime_gpu_cluster_instance_remap_tests)
add_test(
  NAME vice_runtime_gpu_cluster_instance_remap_tests
  COMMAND vice_runtime_gpu_cluster_instance_remap_tests
)

add_executable(
  vice_runtime_gpu_material_resources_tests
  ../runtime_gpu_material_resources_tests.cpp
)
target_include_directories(
  vice_runtime_gpu_material_resources_tests
  PRIVATE
    "${PROJECT_SOURCE_DIR}/engine/runtime/src"
)
target_link_libraries(
  vice_runtime_gpu_material_resources_tests
  PRIVATE
    VICE::Runtime
)
target_compile_features(vice_runtime_gpu_material_resources_tests PRIVATE cxx_std_23)
vice_enable_warnings(vice_runtime_gpu_material_resources_tests)
add_test(
  NAME vice_runtime_gpu_material_resources_tests
  COMMAND vice_runtime_gpu_material_resources_tests
)

add_executable(
  vice_runtime_gpu_instance_material_tests
  ../runtime_gpu_instance_material_tests.cpp
)
target_link_libraries(
  vice_runtime_gpu_instance_material_tests
  PRIVATE
    VICE::Runtime
)
target_compile_features(vice_runtime_gpu_instance_material_tests PRIVATE cxx_std_23)
vice_enable_warnings(vice_runtime_gpu_instance_material_tests)
add_test(
  NAME vice_runtime_gpu_instance_material_tests
  COMMAND vice_runtime_gpu_instance_material_tests
)

add_executable(
  vice_runtime_gpu_pbr_batch_tests
  ../runtime_gpu_pbr_batch_tests.cpp
)
target_include_directories(
  vice_runtime_gpu_pbr_batch_tests
  PRIVATE
    "${PROJECT_SOURCE_DIR}/engine/runtime/src"
)
target_link_libraries(
  vice_runtime_gpu_pbr_batch_tests
  PRIVATE
    VICE::Runtime
)
target_compile_features(vice_runtime_gpu_pbr_batch_tests PRIVATE cxx_std_23)
vice_enable_warnings(vice_runtime_gpu_pbr_batch_tests)
add_test(
  NAME vice_runtime_gpu_pbr_batch_tests
  COMMAND vice_runtime_gpu_pbr_batch_tests
)

add_executable(
  vice_runtime_gpu_shadow_batch_tests
  ../runtime_gpu_shadow_batch_tests.cpp
)
target_include_directories(
  vice_runtime_gpu_shadow_batch_tests
  PRIVATE
    "${PROJECT_SOURCE_DIR}/engine/runtime/src"
)
target_link_libraries(
  vice_runtime_gpu_shadow_batch_tests
  PRIVATE
    VICE::Runtime
)
target_compile_features(vice_runtime_gpu_shadow_batch_tests PRIVATE cxx_std_23)
vice_enable_warnings(vice_runtime_gpu_shadow_batch_tests)
add_test(
  NAME vice_runtime_gpu_shadow_batch_tests
  COMMAND vice_runtime_gpu_shadow_batch_tests
)

add_executable(
  vice_runtime_gpu_motion_history_tests
  ../runtime_gpu_motion_history_tests.cpp
)
target_include_directories(
  vice_runtime_gpu_motion_history_tests
  PRIVATE
    "${PROJECT_SOURCE_DIR}/engine/runtime/src"
)
target_link_libraries(
  vice_runtime_gpu_motion_history_tests
  PRIVATE
    VICE::Runtime
)
target_compile_features(vice_runtime_gpu_motion_history_tests PRIVATE cxx_std_23)
vice_enable_warnings(vice_runtime_gpu_motion_history_tests)
add_test(
  NAME vice_runtime_gpu_motion_history_tests
  COMMAND vice_runtime_gpu_motion_history_tests
)

add_executable(
  vice_runtime_supporting_pass_execution_tests
  ../runtime_supporting_pass_execution_tests.cpp
)
target_link_libraries(
  vice_runtime_supporting_pass_execution_tests
  PRIVATE
    VICE::Runtime
)
target_compile_features(vice_runtime_supporting_pass_execution_tests PRIVATE cxx_std_23)
vice_enable_warnings(vice_runtime_supporting_pass_execution_tests)
add_test(
  NAME vice_runtime_supporting_pass_execution_tests
  COMMAND vice_runtime_supporting_pass_execution_tests
)

add_executable(
  vice_runtime_hiz_tests
  ../runtime_hiz_tests.cpp
)
target_link_libraries(
  vice_runtime_hiz_tests
  PRIVATE
    VICE::Runtime
)
target_compile_features(vice_runtime_hiz_tests PRIVATE cxx_std_23)
vice_enable_warnings(vice_runtime_hiz_tests)
add_test(
  NAME vice_runtime_hiz_tests
  COMMAND vice_runtime_hiz_tests
)

add_executable(
  vice_runtime_hiz_occlusion_tests
  ../runtime_hiz_occlusion_tests.cpp
)
target_link_libraries(
  vice_runtime_hiz_occlusion_tests
  PRIVATE
    VICE::Runtime
)
target_compile_features(vice_runtime_hiz_occlusion_tests PRIVATE cxx_std_23)
vice_enable_warnings(vice_runtime_hiz_occlusion_tests)
add_test(
  NAME vice_runtime_hiz_occlusion_tests
  COMMAND vice_runtime_hiz_occlusion_tests
)

add_executable(
  vice_runtime_hiz_history_tests
  ../runtime_hiz_history_tests.cpp
)
target_include_directories(
  vice_runtime_hiz_history_tests
  PRIVATE
    "${PROJECT_SOURCE_DIR}/engine/runtime/src"
)
target_link_libraries(
  vice_runtime_hiz_history_tests
  PRIVATE
    VICE::Runtime
)
target_compile_features(vice_runtime_hiz_history_tests PRIVATE cxx_std_23)
vice_enable_warnings(vice_runtime_hiz_history_tests)
add_test(
  NAME vice_runtime_hiz_history_tests
  COMMAND vice_runtime_hiz_history_tests
)

if(TARGET vice_mesh_cooker_lib)
  add_executable(
    vice_runtime_scene_tests
    ../runtime_scene_tests.cpp
  )
  target_link_libraries(
    vice_runtime_scene_tests
    PRIVATE
      VICE::MeshCooker
      VICE::Runtime
  )
  target_compile_features(vice_runtime_scene_tests PRIVATE cxx_std_23)
  vice_enable_warnings(vice_runtime_scene_tests)
  add_test(NAME vice_runtime_scene_tests COMMAND vice_runtime_scene_tests)
endif()

add_executable(
  vice_runtime_resource_residency_tests
  ../runtime_resource_residency_tests.cpp
)
target_link_libraries(
  vice_runtime_resource_residency_tests
  PRIVATE
    VICE::Runtime
)
target_compile_features(vice_runtime_resource_residency_tests PRIVATE cxx_std_23)
vice_enable_warnings(vice_runtime_resource_residency_tests)
add_test(
  NAME vice_runtime_resource_residency_tests
  COMMAND vice_runtime_resource_residency_tests
)

add_executable(
  vice_runtime_pre_pbr_motion_tests
  ../runtime_pre_pbr_motion_tests.cpp
)
target_compile_features(vice_runtime_pre_pbr_motion_tests PRIVATE cxx_std_23)
target_compile_definitions(
  vice_runtime_pre_pbr_motion_tests
  PRIVATE
    VICE_RUNTIME_SCENE_LEGACY_SOURCE="${CMAKE_SOURCE_DIR}/engine/runtime/src/scene_legacy.inc"
)
vice_enable_warnings(vice_runtime_pre_pbr_motion_tests)
add_test(
  NAME vice_runtime_pre_pbr_motion_tests
  COMMAND vice_runtime_pre_pbr_motion_tests
)


if(TARGET vice_rigid_body_world AND TARGET vice_world_cooker_lib)
  vice_add_runtime_module_fixture(vice_runtime_gameplay_module test_gameplay_module.cpp)
  add_executable(vice_runtime_gameplay_tests ../runtime_gameplay_tests.cpp)
  target_link_libraries(vice_runtime_gameplay_tests PRIVATE VICE::Runtime VICE::RigidBodyWorld VICE::WorldCooker VICE::Jobs)
  target_compile_definitions(vice_runtime_gameplay_tests PRIVATE VICE_RUNTIME_GAMEPLAY_MODULE="$<TARGET_FILE:vice_runtime_gameplay_module>")
  add_dependencies(vice_runtime_gameplay_tests vice_runtime_gameplay_module)
  target_compile_features(vice_runtime_gameplay_tests PRIVATE cxx_std_23)
  vice_enable_warnings(vice_runtime_gameplay_tests)
  add_test(NAME vice_runtime_gameplay_tests COMMAND vice_runtime_gameplay_tests)
endif()

if(TARGET vice_rigid_body_world AND TARGET vice_world_cooker_lib)
  vice_add_runtime_module_fixture(vice_runtime_support_module test_support_game_module.cpp)
  add_executable(vice_authored_support_tests ../authored_support_tests.cpp)
  target_link_libraries(vice_authored_support_tests PRIVATE VICE::Runtime VICE::RigidBodyWorld VICE::WorldCooker VICE::Jobs)
  target_compile_definitions(vice_authored_support_tests PRIVATE VICE_SUPPORT_TEST_MODULE="$<TARGET_FILE:vice_runtime_support_module>")
  if(TARGET vice_editor_preview)
    target_link_libraries(vice_authored_support_tests PRIVATE VICE::EditorPreview)
    target_compile_definitions(vice_authored_support_tests PRIVATE VICE_SUPPORT_EDITOR_PREVIEW=1)
  endif()
  add_dependencies(vice_authored_support_tests vice_runtime_support_module)
  target_compile_features(vice_authored_support_tests PRIVATE cxx_std_23)
  vice_enable_warnings(vice_authored_support_tests)
  add_test(NAME vice_authored_support_tests COMMAND vice_authored_support_tests)
endif()

if(TARGET vice_rigid_body_world AND TARGET vice_world_cooker_lib)
  vice_add_runtime_module_fixture(vice_distance_link_test_module test_distance_link_module.cpp)
  add_executable(vice_authored_distance_link_tests ../authored_distance_link_tests.cpp)
  target_link_libraries(vice_authored_distance_link_tests PRIVATE VICE::Runtime VICE::RuntimeDiagnostics VICE::RigidBodyWorld VICE::WorldCooker VICE::Jobs)
  if(TARGET vice_editor_preview)
    target_link_libraries(vice_authored_distance_link_tests PRIVATE VICE::EditorPreview)
    target_compile_definitions(vice_authored_distance_link_tests PRIVATE VICE_LINK_EDITOR=1)
  endif()
  target_compile_definitions(vice_authored_distance_link_tests PRIVATE VICE_LINK_TEST_MODULE="$<TARGET_FILE:vice_distance_link_test_module>")
  add_dependencies(vice_authored_distance_link_tests vice_distance_link_test_module)
  target_compile_features(vice_authored_distance_link_tests PRIVATE cxx_std_23)
  vice_enable_warnings(vice_authored_distance_link_tests)
  add_test(NAME vice_authored_distance_link_tests COMMAND vice_authored_distance_link_tests)
endif()

if(TARGET vice_world_cooker_lib)
  vice_add_runtime_module_fixture(vice_dependency_test_module test_dependency_module.cpp)
  add_executable(vice_dependency_tests ../dependency_tests.cpp)
  target_link_libraries(vice_dependency_tests PRIVATE VICE::Runtime VICE::RuntimeDiagnostics VICE::WorldCooker)
  if(TARGET vice_rigid_body_world)
    target_link_libraries(vice_dependency_tests PRIVATE VICE::RigidBodyWorld VICE::Jobs)
    target_compile_definitions(vice_dependency_tests PRIVATE VICE_DEPENDENCY_PHYSICS=1)
  endif()
  if(TARGET vice_editor_preview)
    target_link_libraries(vice_dependency_tests PRIVATE VICE::EditorPreview)
    target_compile_definitions(vice_dependency_tests PRIVATE VICE_DEPENDENCY_EDITOR=1)
  endif()
  target_compile_definitions(vice_dependency_tests PRIVATE VICE_DEPENDENCY_TEST_MODULE="$<TARGET_FILE:vice_dependency_test_module>")
  add_dependencies(vice_dependency_tests vice_dependency_test_module)
  target_compile_features(vice_dependency_tests PRIVATE cxx_std_23)
  vice_enable_warnings(vice_dependency_tests)
  add_test(NAME vice_dependency_tests COMMAND vice_dependency_tests)
endif()

if(TARGET vice_rigid_body_world AND TARGET vice_world_cooker_lib)
  vice_add_runtime_module_fixture(vice_character_test_module test_character_module.cpp)
  add_executable(vice_authored_character_tests ../authored_character_tests.cpp)
  target_link_libraries(vice_authored_character_tests PRIVATE VICE::Runtime VICE::RuntimeDiagnostics VICE::RigidBodyWorld VICE::WorldCooker VICE::Jobs)
  if(TARGET vice_editor_preview)
    target_link_libraries(vice_authored_character_tests PRIVATE VICE::EditorPreview)
    target_compile_definitions(vice_authored_character_tests PRIVATE VICE_CHARACTER_EDITOR=1)
  endif()
  target_compile_definitions(vice_authored_character_tests PRIVATE VICE_CHARACTER_TEST_MODULE="$<TARGET_FILE:vice_character_test_module>")
  add_dependencies(vice_authored_character_tests vice_character_test_module)
  target_compile_features(vice_authored_character_tests PRIVATE cxx_std_23)
  vice_enable_warnings(vice_authored_character_tests)
  add_test(NAME vice_authored_character_tests COMMAND vice_authored_character_tests)
endif()

if(TARGET vice_world_cooker_lib)
  vice_add_runtime_module_fixture(vice_causal_test_module test_causal_module.cpp)
  add_executable(vice_causal_history_tests ../causal_history_tests.cpp)
  target_link_libraries(vice_causal_history_tests PRIVATE VICE::Runtime VICE::RuntimeDiagnostics VICE::WorldCooker)
  if(TARGET vice_rigid_body_world)
    target_link_libraries(vice_causal_history_tests PRIVATE VICE::RigidBodyWorld VICE::Jobs)
    target_compile_definitions(vice_causal_history_tests PRIVATE VICE_CAUSAL_PHYSICS=1)
  endif()
  if(TARGET vice_editor_preview)
    target_link_libraries(vice_causal_history_tests PRIVATE VICE::EditorPreview)
    target_compile_definitions(vice_causal_history_tests PRIVATE VICE_CAUSAL_EDITOR=1)
  endif()
  target_compile_definitions(vice_causal_history_tests PRIVATE VICE_CAUSAL_TEST_MODULE="$<TARGET_FILE:vice_causal_test_module>")
  add_dependencies(vice_causal_history_tests vice_causal_test_module)
  target_compile_features(vice_causal_history_tests PRIVATE cxx_std_23)
  vice_enable_warnings(vice_causal_history_tests)
  add_test(NAME vice_causal_history_tests COMMAND vice_causal_history_tests)
endif()

if(TARGET vice_world_cooker_lib)
  vice_add_runtime_module_fixture(vice_material_test_module test_material_module.cpp)
  add_executable(vice_shared_material_tests ../shared_material_tests.cpp)
  target_link_libraries(vice_shared_material_tests PRIVATE VICE::Runtime VICE::RuntimeDiagnostics VICE::WorldCooker)
  if(TARGET vice_rigid_body_world)
    target_link_libraries(vice_shared_material_tests PRIVATE VICE::RigidBodyWorld VICE::Jobs)
    target_compile_definitions(vice_shared_material_tests PRIVATE VICE_MATERIAL_PHYSICS=1)
  endif()
  if(TARGET vice_editor_preview)
    target_link_libraries(vice_shared_material_tests PRIVATE VICE::EditorPreview VICE::EditorState)
    target_compile_definitions(vice_shared_material_tests PRIVATE VICE_MATERIAL_EDITOR=1)
  endif()
  target_compile_definitions(vice_shared_material_tests PRIVATE VICE_MATERIAL_TEST_MODULE="$<TARGET_FILE:vice_material_test_module>")
  target_compile_definitions(vice_shared_material_tests PRIVATE VICE_MATERIAL_SOURCE="${CMAKE_SOURCE_DIR}/examples/material-playground/assets/worlds/materials.vworld")
  add_dependencies(vice_shared_material_tests vice_material_test_module)
  target_compile_features(vice_shared_material_tests PRIVATE cxx_std_23)
  vice_enable_warnings(vice_shared_material_tests)
  add_test(NAME vice_shared_material_tests COMMAND vice_shared_material_tests)
endif()

if(TARGET vice_world_cooker_lib)
  vice_add_runtime_module_fixture(vice_vehicle_test_module test_vehicle_module.cpp)
  add_executable(vice_authored_vehicle_tests ../authored_vehicle_tests.cpp)
  target_link_libraries(vice_authored_vehicle_tests PRIVATE VICE::Runtime VICE::RuntimeDiagnostics VICE::WorldCooker)
  if(TARGET vice_rigid_body_world)
    target_link_libraries(vice_authored_vehicle_tests PRIVATE VICE::RigidBodyWorld VICE::Jobs)
    target_compile_definitions(vice_authored_vehicle_tests PRIVATE VICE_VEHICLE_PHYSICS=1)
  endif()
  if(TARGET vice_editor_preview)
    target_link_libraries(vice_authored_vehicle_tests PRIVATE VICE::EditorPreview VICE::EditorState)
    target_compile_definitions(vice_authored_vehicle_tests PRIVATE VICE_VEHICLE_EDITOR=1)
  endif()
  target_compile_definitions(vice_authored_vehicle_tests PRIVATE VICE_VEHICLE_TEST_MODULE="$<TARGET_FILE:vice_vehicle_test_module>")
  add_dependencies(vice_authored_vehicle_tests vice_vehicle_test_module)
  target_compile_features(vice_authored_vehicle_tests PRIVATE cxx_std_23)
  vice_enable_warnings(vice_authored_vehicle_tests)
  add_test(NAME vice_authored_vehicle_tests COMMAND vice_authored_vehicle_tests)
endif()

if(TARGET vice_world_cooker_lib)
  vice_add_runtime_module_fixture(vice_interaction_test_module test_interaction_module.cpp)
  add_executable(vice_character_attachment_tests ../character_attachment_tests.cpp)
  target_link_libraries(vice_character_attachment_tests PRIVATE VICE::Runtime VICE::RuntimeDiagnostics VICE::WorldCooker)
  if(TARGET vice_rigid_body_world)
    target_link_libraries(vice_character_attachment_tests PRIVATE VICE::RigidBodyWorld VICE::Jobs)
    target_compile_definitions(vice_character_attachment_tests PRIVATE VICE_ATTACHMENT_PHYSICS=1)
  endif()
  if(TARGET vice_editor_preview)
    target_link_libraries(vice_character_attachment_tests PRIVATE VICE::EditorPreview VICE::EditorState)
    target_compile_definitions(vice_character_attachment_tests PRIVATE VICE_ATTACHMENT_EDITOR=1)
  endif()
  target_compile_definitions(vice_character_attachment_tests PRIVATE VICE_INTERACTION_TEST_MODULE="$<TARGET_FILE:vice_interaction_test_module>")
  add_dependencies(vice_character_attachment_tests vice_interaction_test_module)
  target_compile_features(vice_character_attachment_tests PRIVATE cxx_std_23)
  vice_enable_warnings(vice_character_attachment_tests)
  add_test(NAME vice_character_attachment_tests COMMAND vice_character_attachment_tests)
endif()

if(TARGET vice_presentation_cooker)
  vice_add_runtime_module_fixture(vice_presentation_test_module test_presentation_module.cpp)
  add_executable(vice_presentation_tests ../presentation_tests.cpp)
  add_executable(vice_presentation_runtime_tests ../presentation_runtime_tests.cpp)
  foreach(target vice_presentation_tests vice_presentation_runtime_tests)
    target_link_libraries(${target} PRIVATE VICE::Runtime VICE::RuntimeDiagnostics VICE::PresentationCooker VICE::MeshCooker VICE::WorldCooker)
    target_compile_definitions(${target} PRIVATE VICE_PRESENTATION_EXAMPLE="${CMAKE_SOURCE_DIR}/examples/presentation-playground")
    target_compile_features(${target} PRIVATE cxx_std_23)
    vice_enable_warnings(${target})
    add_test(NAME ${target} COMMAND ${target})
  endforeach()
  target_compile_definitions(vice_presentation_runtime_tests PRIVATE VICE_PRESENTATION_TEST_MODULE="$<TARGET_FILE:vice_presentation_test_module>")
  add_dependencies(vice_presentation_runtime_tests vice_presentation_test_module)
  if(TARGET vice_editor_preview)
    target_link_libraries(vice_presentation_runtime_tests PRIVATE VICE::EditorPreview)
    target_compile_definitions(vice_presentation_runtime_tests PRIVATE VICE_PRESENTATION_EDITOR=1)
  endif()
endif()

if(TARGET vice_world_cooker_lib)
  add_executable(vice_component_payload_tests ../component_payload_tests.cpp)
  target_link_libraries(vice_component_payload_tests PRIVATE VICE::WorldCooker)
  target_compile_features(vice_component_payload_tests PUBLIC cxx_std_23)
  vice_enable_warnings(vice_component_payload_tests)
  add_test(NAME vice_component_payload_tests COMMAND vice_component_payload_tests)
endif()
if(TARGET vice_presentation_cooker)
  vice_add_runtime_module_fixture(vice_component_test_module test_component_module.cpp)
  add_executable(vice_component_runtime_tests ../component_runtime_tests.cpp)
  target_link_libraries(vice_component_runtime_tests PRIVATE VICE::Runtime VICE::RuntimeDiagnostics VICE::PresentationCooker VICE::MeshCooker VICE::WorldCooker)
  target_compile_definitions(vice_component_runtime_tests PRIVATE VICE_PRESENTATION_EXAMPLE="${CMAKE_SOURCE_DIR}/examples/presentation-playground" VICE_COMPONENT_TEST_MODULE="$<TARGET_FILE:vice_component_test_module>")
  add_dependencies(vice_component_runtime_tests vice_component_test_module)
  if(TARGET vice_editor_preview)
    target_link_libraries(vice_component_runtime_tests PRIVATE VICE::EditorPreview VICE::EditorState)
    target_compile_definitions(vice_component_runtime_tests PRIVATE VICE_COMPONENT_EDITOR=1)
  endif()
  vice_enable_warnings(vice_component_runtime_tests)
  add_test(NAME vice_component_runtime_tests COMMAND vice_component_runtime_tests)
endif()
