add_executable(
  vice_ray_tracing_rhi_tests
  ray_tracing_rhi_tests.cpp
)
target_link_libraries(vice_ray_tracing_rhi_tests PRIVATE VICE::Render)
target_compile_features(vice_ray_tracing_rhi_tests PRIVATE cxx_std_23)
vice_enable_warnings(vice_ray_tracing_rhi_tests)
add_test(
  NAME vice_ray_tracing_rhi_tests
  COMMAND vice_ray_tracing_rhi_tests
)
