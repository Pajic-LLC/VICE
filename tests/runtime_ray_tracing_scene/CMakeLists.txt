add_executable(
  vice_runtime_ray_tracing_scene_tests
  ../runtime_ray_tracing_scene_tests.cpp
)
target_link_libraries(
  vice_runtime_ray_tracing_scene_tests
  PRIVATE
    VICE::Runtime
)
target_compile_features(vice_runtime_ray_tracing_scene_tests PRIVATE cxx_std_23)
vice_enable_warnings(vice_runtime_ray_tracing_scene_tests)
add_test(
  NAME vice_runtime_ray_tracing_scene_tests
  COMMAND vice_runtime_ray_tracing_scene_tests
)

add_executable(
  vice_runtime_ray_tracing_scene_executor_tests
  ../runtime_ray_tracing_scene_executor_tests.cpp
)
target_link_libraries(
  vice_runtime_ray_tracing_scene_executor_tests
  PRIVATE
    VICE::Runtime
)
target_compile_features(vice_runtime_ray_tracing_scene_executor_tests PRIVATE cxx_std_23)
vice_enable_warnings(vice_runtime_ray_tracing_scene_executor_tests)
add_test(
  NAME vice_runtime_ray_tracing_scene_executor_tests
  COMMAND vice_runtime_ray_tracing_scene_executor_tests
)
