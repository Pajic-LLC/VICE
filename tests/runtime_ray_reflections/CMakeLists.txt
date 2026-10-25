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
    VICE_RAY_REFLECTIONS_CPP_SOURCE="${CMAKE_SOURCE_DIR}/engine/runtime/src/ray_reflections.cpp"
    VICE_RAY_REFLECTIONS_BRIDGE_SOURCE="${CMAKE_SOURCE_DIR}/engine/runtime/src/reflection_surface_guide_bridge.hpp"
)
vice_enable_warnings(vice_runtime_ray_reflections_tests)
add_test(
  NAME vice_runtime_ray_reflections_tests
  COMMAND vice_runtime_ray_reflections_tests
)
