add_executable(
  vice_runtime_directional_ray_shadow_tests
  runtime_directional_ray_shadow_tests.cpp
)
target_link_libraries(
  vice_runtime_directional_ray_shadow_tests
  PRIVATE
    VICE::Runtime
)
target_compile_features(vice_runtime_directional_ray_shadow_tests PRIVATE cxx_std_23)
vice_enable_warnings(vice_runtime_directional_ray_shadow_tests)
add_test(
  NAME vice_runtime_directional_ray_shadow_tests
  COMMAND vice_runtime_directional_ray_shadow_tests
)

if(DEFINED VICE_RUNTIME_DIRECTIONAL_RAY_SHADOW_MANIFEST_PATH AND
   TARGET vice_runtime_directional_ray_shadow_shader)
  add_executable(
    vice_runtime_directional_ray_shadow_shader_tests
    runtime_directional_ray_shadow_shader_tests.cpp
  )
  target_link_libraries(
    vice_runtime_directional_ray_shadow_shader_tests
    PRIVATE
      VICE::Render
  )
  target_compile_features(vice_runtime_directional_ray_shadow_shader_tests PRIVATE cxx_std_23)
  target_compile_definitions(
    vice_runtime_directional_ray_shadow_shader_tests
    PRIVATE
      VICE_DIRECTIONAL_RAY_SHADOW_MANIFEST="${VICE_RUNTIME_DIRECTIONAL_RAY_SHADOW_MANIFEST_PATH}"
  )
  add_dependencies(
    vice_runtime_directional_ray_shadow_shader_tests
    vice_runtime_directional_ray_shadow_shader
  )
  vice_enable_warnings(vice_runtime_directional_ray_shadow_shader_tests)
  add_test(
    NAME vice_runtime_directional_ray_shadow_shader_tests
    COMMAND vice_runtime_directional_ray_shadow_shader_tests
  )
endif()