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
    VICE_RAY_REFLECTION_HISTORY_CPP_SOURCE="${CMAKE_SOURCE_DIR}/engine/runtime/src/ray_reflection_history.cpp"
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
  add_executable(
    vice_runtime_ray_reflection_backend_execution_tests
    runtime_ray_reflection_backend_execution_tests.cpp
  )
  add_dependencies(
    vice_runtime_ray_reflection_backend_execution_tests
    vice_runtime_ray_reflections_shader
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
  )
  vice_enable_warnings(vice_runtime_ray_reflection_backend_execution_tests)
  add_test(
    NAME vice_runtime_ray_reflection_backend_execution_tests
    COMMAND vice_runtime_ray_reflection_backend_execution_tests
  )
endif()
