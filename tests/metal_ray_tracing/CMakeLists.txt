enable_language(OBJCXX)
find_library(METAL_FRAMEWORK_RAY_TEST Metal REQUIRED)

add_executable(
  vice_metal_ray_tracing_tests
  ../metal_ray_tracing_tests.mm
)
target_include_directories(
  vice_metal_ray_tracing_tests
  PRIVATE
    ${CMAKE_SOURCE_DIR}/engine/render/src/metal
)
target_link_libraries(
  vice_metal_ray_tracing_tests
  PRIVATE
    VICE::Render
    ${METAL_FRAMEWORK_RAY_TEST}
)
target_compile_features(vice_metal_ray_tracing_tests PRIVATE cxx_std_23)
set_target_properties(
  vice_metal_ray_tracing_tests
  PROPERTIES
    OBJCXX_STANDARD 23
    OBJCXX_STANDARD_REQUIRED YES
    OBJCXX_EXTENSIONS NO
)
set_source_files_properties(
  ../metal_ray_tracing_tests.mm
  PROPERTIES COMPILE_OPTIONS "-fobjc-arc"
)
vice_enable_warnings(vice_metal_ray_tracing_tests)
add_test(
  NAME vice_metal_ray_tracing_tests
  COMMAND vice_metal_ray_tracing_tests
)
