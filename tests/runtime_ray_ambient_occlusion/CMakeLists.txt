add_executable(
  vice_runtime_ray_ambient_occlusion_tests
  runtime_ray_ambient_occlusion_tests.cpp
)
target_link_libraries(
  vice_runtime_ray_ambient_occlusion_tests
  PRIVATE
    VICE::Runtime
)
target_compile_features(vice_runtime_ray_ambient_occlusion_tests PRIVATE cxx_std_23)
target_compile_definitions(
  vice_runtime_ray_ambient_occlusion_tests
  PRIVATE
    VICE_RUNTIME_SCENE_SOURCE="${CMAKE_SOURCE_DIR}/engine/runtime/src/scene.cpp"
)
vice_enable_warnings(vice_runtime_ray_ambient_occlusion_tests)
add_test(
  NAME vice_runtime_ray_ambient_occlusion_tests
  COMMAND vice_runtime_ray_ambient_occlusion_tests
)

add_executable(
  vice_runtime_ray_ambient_occlusion_denoise_tests
  runtime_ray_ambient_occlusion_denoise_tests.cpp
)
target_link_libraries(
  vice_runtime_ray_ambient_occlusion_denoise_tests
  PRIVATE
    VICE::Runtime
)
target_compile_features(vice_runtime_ray_ambient_occlusion_denoise_tests PRIVATE cxx_std_23)
vice_enable_warnings(vice_runtime_ray_ambient_occlusion_denoise_tests)
add_test(
  NAME vice_runtime_ray_ambient_occlusion_denoise_tests
  COMMAND vice_runtime_ray_ambient_occlusion_denoise_tests
)

add_executable(
  vice_runtime_ray_ambient_occlusion_temporal_tests
  runtime_ray_ambient_occlusion_temporal_tests.cpp
)
target_link_libraries(
  vice_runtime_ray_ambient_occlusion_temporal_tests
  PRIVATE
    VICE::Runtime
)
target_compile_features(vice_runtime_ray_ambient_occlusion_temporal_tests PRIVATE cxx_std_23)
target_compile_definitions(
  vice_runtime_ray_ambient_occlusion_temporal_tests
  PRIVATE
    VICE_RUNTIME_SCENE_SOURCE="${CMAKE_SOURCE_DIR}/engine/runtime/src/scene.cpp"
)
vice_enable_warnings(vice_runtime_ray_ambient_occlusion_temporal_tests)
add_test(
  NAME vice_runtime_ray_ambient_occlusion_temporal_tests
  COMMAND vice_runtime_ray_ambient_occlusion_temporal_tests
)

if(DEFINED VICE_RUNTIME_RAY_AMBIENT_OCCLUSION_MANIFEST_PATH AND
   TARGET vice_runtime_ray_ambient_occlusion_shader)
  add_executable(
    vice_runtime_ray_ambient_occlusion_shader_tests
    runtime_ray_ambient_occlusion_shader_tests.cpp
  )
  target_link_libraries(
    vice_runtime_ray_ambient_occlusion_shader_tests
    PRIVATE
      VICE::Render
  )
  target_compile_features(vice_runtime_ray_ambient_occlusion_shader_tests PRIVATE cxx_std_23)
  target_compile_definitions(
    vice_runtime_ray_ambient_occlusion_shader_tests
    PRIVATE
      VICE_RAY_AMBIENT_OCCLUSION_MANIFEST="${VICE_RUNTIME_RAY_AMBIENT_OCCLUSION_MANIFEST_PATH}"
      VICE_RAY_AMBIENT_OCCLUSION_SOURCE="${CMAKE_SOURCE_DIR}/assets/shaders/vice_ray_ambient_occlusion.hlsl"
      VICE_GPU_PBR_SURFACE_SOURCE="${CMAKE_SOURCE_DIR}/assets/shaders/vice_gpu_pbr_surface.hlsl"
      VICE_PROJECT_SURFACE_SOURCE="${CMAKE_SOURCE_DIR}/assets/shaders/vice_project_surface.hlsl"
      VICE_RUNTIME_SCENE_LEGACY_SOURCE="${CMAKE_SOURCE_DIR}/engine/runtime/src/scene_legacy.inc"
  )
  add_dependencies(
    vice_runtime_ray_ambient_occlusion_shader_tests
    vice_runtime_ray_ambient_occlusion_shader
  )
  vice_enable_warnings(vice_runtime_ray_ambient_occlusion_shader_tests)
  add_test(
    NAME vice_runtime_ray_ambient_occlusion_shader_tests
    COMMAND vice_runtime_ray_ambient_occlusion_shader_tests
  )
endif()


if(DEFINED VICE_RUNTIME_RAY_AMBIENT_OCCLUSION_DENOISE_MANIFEST_PATH AND
   TARGET vice_runtime_ray_ambient_occlusion_denoise_shader)
  add_executable(
    vice_runtime_ray_ambient_occlusion_denoise_shader_tests
    runtime_ray_ambient_occlusion_denoise_shader_tests.cpp
  )
  target_link_libraries(
    vice_runtime_ray_ambient_occlusion_denoise_shader_tests
    PRIVATE
      VICE::Render
  )
  target_compile_features(vice_runtime_ray_ambient_occlusion_denoise_shader_tests PRIVATE cxx_std_23)
  target_compile_definitions(
    vice_runtime_ray_ambient_occlusion_denoise_shader_tests
    PRIVATE
      VICE_RAY_AMBIENT_OCCLUSION_DENOISE_MANIFEST="${VICE_RUNTIME_RAY_AMBIENT_OCCLUSION_DENOISE_MANIFEST_PATH}"
      VICE_RAY_AMBIENT_OCCLUSION_DENOISE_SOURCE="${CMAKE_SOURCE_DIR}/assets/shaders/vice_ray_ambient_occlusion_denoise.hlsl"
  )
  add_dependencies(
    vice_runtime_ray_ambient_occlusion_denoise_shader_tests
    vice_runtime_ray_ambient_occlusion_denoise_shader
  )
  vice_enable_warnings(vice_runtime_ray_ambient_occlusion_denoise_shader_tests)
  add_test(
    NAME vice_runtime_ray_ambient_occlusion_denoise_shader_tests
    COMMAND vice_runtime_ray_ambient_occlusion_denoise_shader_tests
  )
endif()


if(DEFINED VICE_RUNTIME_RAY_AMBIENT_OCCLUSION_TEMPORAL_MANIFEST_PATH AND
   TARGET vice_runtime_ray_ambient_occlusion_temporal_shader)
  add_executable(
    vice_runtime_ray_ambient_occlusion_temporal_shader_tests
    runtime_ray_ambient_occlusion_temporal_shader_tests.cpp
  )
  target_link_libraries(
    vice_runtime_ray_ambient_occlusion_temporal_shader_tests
    PRIVATE
      VICE::Render
  )
  target_compile_features(vice_runtime_ray_ambient_occlusion_temporal_shader_tests PRIVATE cxx_std_23)
  target_compile_definitions(
    vice_runtime_ray_ambient_occlusion_temporal_shader_tests
    PRIVATE
      VICE_RAY_AMBIENT_OCCLUSION_TEMPORAL_MANIFEST="${VICE_RUNTIME_RAY_AMBIENT_OCCLUSION_TEMPORAL_MANIFEST_PATH}"
      VICE_RAY_AMBIENT_OCCLUSION_TEMPORAL_SOURCE="${CMAKE_SOURCE_DIR}/assets/shaders/vice_ray_ambient_occlusion_temporal.hlsl"
  )
  add_dependencies(
    vice_runtime_ray_ambient_occlusion_temporal_shader_tests
    vice_runtime_ray_ambient_occlusion_temporal_shader
  )
  vice_enable_warnings(vice_runtime_ray_ambient_occlusion_temporal_shader_tests)
  add_test(
    NAME vice_runtime_ray_ambient_occlusion_temporal_shader_tests
    COMMAND vice_runtime_ray_ambient_occlusion_temporal_shader_tests
  )
endif()
