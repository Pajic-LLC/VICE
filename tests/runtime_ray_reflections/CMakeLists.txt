add_executable(
  vice_runtime_ray_reflections_tests
  runtime_ray_reflections_tests.cpp
)
target_link_libraries(
  vice_runtime_ray_reflections_tests
  PRIVATE
    VICE::Runtime
)
target_compile_features(vice_runtime_ray_reflections_tests PRIVATE cxx_std_23)
target_compile_definitions(
  vice_runtime_ray_reflections_tests
  PRIVATE
    VICE_RAY_REFLECTIONS_SHADER_SOURCE="${CMAKE_SOURCE_DIR}/assets/shaders/vice_ray_reflections.hlsl"
    VICE_RAY_REFLECTION_HISTORY_SHADER_SOURCE="${CMAKE_SOURCE_DIR}/assets/shaders/vice_ray_reflection_history.hlsl"
    VICE_GPU_PBR_SURFACE_SHADER_SOURCE="${CMAKE_SOURCE_DIR}/assets/shaders/vice_gpu_pbr_surface.hlsl"
    VICE_RAY_REFLECTIONS_CPP_SOURCE="${CMAKE_SOURCE_DIR}/engine/runtime/src/ray_reflections.cpp"
    VICE_RAY_REFLECTION_HISTORY_CPP_SOURCE="${CMAKE_SOURCE_DIR}/engine/runtime/src/ray_hit_radiance_history.cpp"
    VICE_RAY_REFLECTIONS_BRIDGE_SOURCE="${CMAKE_SOURCE_DIR}/engine/runtime/src/reflection_surface_guide_bridge.hpp"
)
vice_enable_warnings(vice_runtime_ray_reflections_tests)
add_test(
  NAME vice_runtime_ray_reflections_tests
  COMMAND vice_runtime_ray_reflections_tests
)

add_executable(
  vice_runtime_ray_reflection_scene_index_tests
  runtime_ray_reflection_scene_index_tests.cpp
)
target_compile_features(vice_runtime_ray_reflection_scene_index_tests PRIVATE cxx_std_23)
target_compile_definitions(
  vice_runtime_ray_reflection_scene_index_tests
  PRIVATE
    VICE_RAY_REFLECTIONS_SHADER_SOURCE="${CMAKE_SOURCE_DIR}/assets/shaders/vice_ray_reflections.hlsl"
    VICE_RAY_REFLECTIONS_CPP_SOURCE="${CMAKE_SOURCE_DIR}/engine/runtime/src/ray_reflections.cpp"
)
vice_enable_warnings(vice_runtime_ray_reflection_scene_index_tests)
add_test(
  NAME vice_runtime_ray_reflection_scene_index_tests
  COMMAND vice_runtime_ray_reflection_scene_index_tests
)

add_executable(
  vice_runtime_ray_reflection_reconstruction_tests
  runtime_ray_reflection_reconstruction_tests.cpp
)
target_compile_features(vice_runtime_ray_reflection_reconstruction_tests PRIVATE cxx_std_23)
target_compile_definitions(
  vice_runtime_ray_reflection_reconstruction_tests
  PRIVATE
    VICE_RAY_REFLECTION_HISTORY_SHADER_SOURCE="${CMAKE_SOURCE_DIR}/assets/shaders/vice_ray_reflection_history.hlsl"
)
vice_enable_warnings(vice_runtime_ray_reflection_reconstruction_tests)
add_test(
  NAME vice_runtime_ray_reflection_reconstruction_tests
  COMMAND vice_runtime_ray_reflection_reconstruction_tests
)

add_executable(
  vice_runtime_ray_diffuse_gi_tests
  runtime_ray_diffuse_gi_tests.cpp
)
target_link_libraries(
  vice_runtime_ray_diffuse_gi_tests
  PRIVATE
    VICE::Runtime
)
target_compile_features(vice_runtime_ray_diffuse_gi_tests PRIVATE cxx_std_23)
target_compile_definitions(
  vice_runtime_ray_diffuse_gi_tests
  PRIVATE
    VICE_RAY_DIFFUSE_GI_SHADER_SOURCE="${CMAKE_SOURCE_DIR}/assets/shaders/vice_ray_diffuse_gi.hlsl"
    VICE_RAY_DIFFUSE_GI_HISTORY_SHADER_SOURCE="${CMAKE_SOURCE_DIR}/assets/shaders/vice_ray_reflection_history.hlsl"
    VICE_RAY_HIT_RADIANCE_HISTORY_CPP_SOURCE="${CMAKE_SOURCE_DIR}/engine/runtime/src/ray_hit_radiance_history.cpp"
)
vice_enable_warnings(vice_runtime_ray_diffuse_gi_tests)
add_test(
  NAME vice_runtime_ray_diffuse_gi_tests
  COMMAND vice_runtime_ray_diffuse_gi_tests
)

add_executable(
  vice_runtime_ray_diffuse_gi_settings_tests
  runtime_ray_diffuse_gi_settings_tests.cpp
)
target_link_libraries(
  vice_runtime_ray_diffuse_gi_settings_tests
  PRIVATE
    VICE::Runtime
)
target_compile_features(
  vice_runtime_ray_diffuse_gi_settings_tests
  PRIVATE
    cxx_std_23
)
vice_enable_warnings(vice_runtime_ray_diffuse_gi_settings_tests)
add_test(
  NAME vice_runtime_ray_diffuse_gi_settings_tests
  COMMAND vice_runtime_ray_diffuse_gi_settings_tests
)

add_executable(
  vice_runtime_ray_diffuse_gi_pipeline_contract_tests
  runtime_ray_diffuse_gi_pipeline_contract_tests.cpp
)
target_compile_features(
  vice_runtime_ray_diffuse_gi_pipeline_contract_tests
  PRIVATE
    cxx_std_23
)
target_compile_definitions(
  vice_runtime_ray_diffuse_gi_pipeline_contract_tests
  PRIVATE
    VICE_RAY_DIFFUSE_GI_PIPELINE_CPP_SOURCE="${CMAKE_SOURCE_DIR}/engine/runtime/src/ray_diffuse_gi_pipeline.cpp"
)
vice_enable_warnings(vice_runtime_ray_diffuse_gi_pipeline_contract_tests)
add_test(
  NAME vice_runtime_ray_diffuse_gi_pipeline_contract_tests
  COMMAND vice_runtime_ray_diffuse_gi_pipeline_contract_tests
)

add_executable(
  vice_runtime_ray_diffuse_gi_reconstruction_tests
  runtime_ray_diffuse_gi_reconstruction_tests.cpp
)
target_link_libraries(
  vice_runtime_ray_diffuse_gi_reconstruction_tests
  PRIVATE
    VICE::Runtime
)
target_compile_features(
  vice_runtime_ray_diffuse_gi_reconstruction_tests
  PRIVATE
    cxx_std_23
)
target_compile_definitions(
  vice_runtime_ray_diffuse_gi_reconstruction_tests
  PRIVATE
    VICE_RAY_DIFFUSE_GI_DENOISE_SHADER_SOURCE="${CMAKE_SOURCE_DIR}/assets/shaders/vice_ray_diffuse_gi_denoise.hlsl"
    VICE_RAY_DIFFUSE_GI_TEMPORAL_SHADER_SOURCE="${CMAKE_SOURCE_DIR}/assets/shaders/vice_ray_diffuse_gi_temporal.hlsl"
)
vice_enable_warnings(vice_runtime_ray_diffuse_gi_reconstruction_tests)
add_test(
  NAME vice_runtime_ray_diffuse_gi_reconstruction_tests
  COMMAND vice_runtime_ray_diffuse_gi_reconstruction_tests
)

if(TARGET vice_runtime_ray_diffuse_gi_shader)
  add_dependencies(
    vice_runtime_ray_diffuse_gi_tests
    vice_runtime_ray_diffuse_gi_shader
  )
  target_compile_definitions(
    vice_runtime_ray_diffuse_gi_tests
    PRIVATE
      VICE_RUNTIME_RAY_DIFFUSE_GI_CONTRACT_MANIFEST="${VICE_RUNTIME_RAY_DIFFUSE_GI_MANIFEST_PATH}"
  )
endif()

if(TARGET vice_runtime_ray_diffuse_gi_denoise_shader AND
   TARGET vice_runtime_ray_diffuse_gi_temporal_shader)
  add_dependencies(
    vice_runtime_ray_diffuse_gi_reconstruction_tests
    vice_runtime_ray_diffuse_gi_denoise_shader
    vice_runtime_ray_diffuse_gi_temporal_shader
  )
  target_compile_definitions(
    vice_runtime_ray_diffuse_gi_reconstruction_tests
    PRIVATE
      VICE_RUNTIME_RAY_DIFFUSE_GI_DENOISE_MANIFEST="${VICE_RUNTIME_RAY_DIFFUSE_GI_DENOISE_MANIFEST_PATH}"
      VICE_RUNTIME_RAY_DIFFUSE_GI_TEMPORAL_MANIFEST="${VICE_RUNTIME_RAY_DIFFUSE_GI_TEMPORAL_MANIFEST_PATH}"
  )
endif()

if(TARGET vice_runtime_ray_reflection_history_shader)
  add_dependencies(
    vice_runtime_ray_diffuse_gi_tests
    vice_runtime_ray_reflection_history_shader
  )
  target_compile_definitions(
    vice_runtime_ray_diffuse_gi_tests
    PRIVATE
      VICE_RUNTIME_RAY_DIFFUSE_GI_HISTORY_MANIFEST="${VICE_RUNTIME_RAY_REFLECTION_HISTORY_MANIFEST_PATH}"
  )
endif()

add_executable(
  vice_runtime_ray_reflection_backend_fallback_tests
  runtime_ray_reflection_backend_fallback_tests.cpp
)
target_link_libraries(
  vice_runtime_ray_reflection_backend_fallback_tests
  PRIVATE
    VICE::Runtime
)
target_compile_features(
  vice_runtime_ray_reflection_backend_fallback_tests
  PRIVATE
    cxx_std_23
)
vice_enable_warnings(vice_runtime_ray_reflection_backend_fallback_tests)
add_test(
  NAME vice_runtime_ray_reflection_backend_fallback_tests
  COMMAND vice_runtime_ray_reflection_backend_fallback_tests
)

if(TARGET vice_runtime_ray_reflections_shader)
  set(
    _vice_runtime_ray_reflection_validation_readback_output
    "${CMAKE_BINARY_DIR}/cooked/vice_runtime_ray_reflection_validation_readback"
  )
  set(
    VICE_RUNTIME_RAY_REFLECTION_VALIDATION_READBACK_MANIFEST_PATH
    "${_vice_runtime_ray_reflection_validation_readback_output}/shader.vice.json"
  )
  vice_add_cooked_shader(
    NAME vice_runtime_ray_reflection_validation_readback_shader
    INPUT "${CMAKE_CURRENT_SOURCE_DIR}/reflection_radiance_readback.hlsl"
    OUTPUT "${_vice_runtime_ray_reflection_validation_readback_output}"
    ENTRY vice_reflection_radiance_readback_cs
    STAGE compute
    TARGET "${_vice_runtime_ray_shadow_target}"
    SHADER_MODEL 6.6
    DXC "${_vice_runtime_ray_shadow_dxc}"
    METAL_SHADER_CONVERTER "${_vice_runtime_ray_shadow_metal_converter}"
  )

  add_executable(
    vice_runtime_ray_reflection_backend_execution_tests
    runtime_ray_reflection_backend_execution_tests.cpp
  )
  add_dependencies(
    vice_runtime_ray_reflection_backend_execution_tests
    vice_runtime_ray_reflections_shader
    vice_runtime_ray_reflection_validation_readback_shader
  )
  target_link_libraries(
    vice_runtime_ray_reflection_backend_execution_tests
    PRIVATE
      VICE::Runtime
  )
  target_compile_features(
    vice_runtime_ray_reflection_backend_execution_tests
    PRIVATE
      cxx_std_23
  )
  target_compile_definitions(
    vice_runtime_ray_reflection_backend_execution_tests
    PRIVATE
      VICE_RUNTIME_RAY_REFLECTIONS_MANIFEST="${VICE_RUNTIME_RAY_REFLECTIONS_MANIFEST_PATH}"
      VICE_RUNTIME_RAY_REFLECTION_VALIDATION_READBACK_MANIFEST="${VICE_RUNTIME_RAY_REFLECTION_VALIDATION_READBACK_MANIFEST_PATH}"
  )
  vice_enable_warnings(vice_runtime_ray_reflection_backend_execution_tests)
  add_test(
    NAME vice_runtime_ray_reflection_backend_execution_tests
    COMMAND vice_runtime_ray_reflection_backend_execution_tests
  )

  if(TARGET vice_runtime_ray_diffuse_gi_shader)
    add_executable(
      vice_runtime_ray_diffuse_gi_backend_execution_tests
      runtime_ray_diffuse_gi_backend_execution_tests.cpp
    )
    add_dependencies(
      vice_runtime_ray_diffuse_gi_backend_execution_tests
      vice_runtime_ray_diffuse_gi_shader
      vice_runtime_ray_reflection_validation_readback_shader
    )
    target_link_libraries(
      vice_runtime_ray_diffuse_gi_backend_execution_tests
      PRIVATE
        VICE::Runtime
    )
    target_compile_features(
      vice_runtime_ray_diffuse_gi_backend_execution_tests
      PRIVATE
        cxx_std_23
    )
    target_compile_definitions(
      vice_runtime_ray_diffuse_gi_backend_execution_tests
      PRIVATE
        VICE_RUNTIME_RAY_DIFFUSE_GI_MANIFEST="${VICE_RUNTIME_RAY_DIFFUSE_GI_MANIFEST_PATH}"
        VICE_RUNTIME_RAY_REFLECTION_VALIDATION_READBACK_MANIFEST="${VICE_RUNTIME_RAY_REFLECTION_VALIDATION_READBACK_MANIFEST_PATH}"
    )
    vice_enable_warnings(vice_runtime_ray_diffuse_gi_backend_execution_tests)
    add_test(
      NAME vice_runtime_ray_diffuse_gi_backend_execution_tests
      COMMAND vice_runtime_ray_diffuse_gi_backend_execution_tests
    )
  endif()

  if(TARGET vice_runtime_ray_reflection_history_shader)
    add_executable(
      vice_runtime_ray_reflection_history_backend_execution_tests
      runtime_ray_reflection_history_backend_execution_tests.cpp
    )
    add_dependencies(
      vice_runtime_ray_reflection_history_backend_execution_tests
      vice_runtime_ray_reflection_history_shader
      vice_runtime_ray_reflection_validation_readback_shader
    )
    target_link_libraries(
      vice_runtime_ray_reflection_history_backend_execution_tests
      PRIVATE
        VICE::Runtime
    )
    target_compile_features(
      vice_runtime_ray_reflection_history_backend_execution_tests
      PRIVATE
        cxx_std_23
    )
    target_compile_definitions(
      vice_runtime_ray_reflection_history_backend_execution_tests
      PRIVATE
        VICE_RUNTIME_RAY_REFLECTION_HISTORY_MANIFEST="${VICE_RUNTIME_RAY_REFLECTION_HISTORY_MANIFEST_PATH}"
        VICE_RUNTIME_RAY_REFLECTION_VALIDATION_READBACK_MANIFEST="${VICE_RUNTIME_RAY_REFLECTION_VALIDATION_READBACK_MANIFEST_PATH}"
    )
    vice_enable_warnings(vice_runtime_ray_reflection_history_backend_execution_tests)
    add_test(
      NAME vice_runtime_ray_reflection_history_backend_execution_tests
      COMMAND vice_runtime_ray_reflection_history_backend_execution_tests
    )
  endif()
endif()
