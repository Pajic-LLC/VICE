add_executable(
  vice_d3d12_ray_tracing_tests
  d3d12_ray_tracing_tests.cpp
)
target_include_directories(
  vice_d3d12_ray_tracing_tests
  PRIVATE
    ${CMAKE_SOURCE_DIR}/engine/render/src/d3d12
)
target_link_libraries(
  vice_d3d12_ray_tracing_tests
  PRIVATE
    VICE::Render
    d3d12
    dxgi
)
target_compile_features(vice_d3d12_ray_tracing_tests PRIVATE cxx_std_23)
vice_enable_warnings(vice_d3d12_ray_tracing_tests)
add_test(
  NAME vice_d3d12_ray_tracing_tests
  COMMAND vice_d3d12_ray_tracing_tests
)
