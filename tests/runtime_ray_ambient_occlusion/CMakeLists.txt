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
vice_enable_warnings(vice_runtime_ray_ambient_occlusion_tests)
add_test(
  NAME vice_runtime_ray_ambient_occlusion_tests
  COMMAND vice_runtime_ray_ambient_occlusion_tests
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
