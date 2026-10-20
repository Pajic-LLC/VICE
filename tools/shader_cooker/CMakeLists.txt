add_library(
  vice_shader_cooker_lib STATIC
  src/shader_cooker.cpp
  src/compute_reflection.cpp
  src/metal_reflection.cpp
)
add_library(VICE::ShaderCooker ALIAS vice_shader_cooker_lib)

target_include_directories(
  vice_shader_cooker_lib
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_compile_features(vice_shader_cooker_lib PUBLIC cxx_std_23)
vice_enable_warnings(vice_shader_cooker_lib)

add_executable(vice_shader_cooker src/main.cpp)
target_link_libraries(vice_shader_cooker PRIVATE VICE::ShaderCooker)
target_compile_features(vice_shader_cooker PRIVATE cxx_std_23)
vice_enable_warnings(vice_shader_cooker)

if(VICE_BUILD_TESTS)
  add_executable(
    vice_shader_compute_reflection_tests
    ${CMAKE_SOURCE_DIR}/tests/shader_compute_reflection_tests.cpp
  )
  target_link_libraries(vice_shader_compute_reflection_tests PRIVATE VICE::ShaderCooker)
  target_compile_features(vice_shader_compute_reflection_tests PRIVATE cxx_std_23)
  vice_enable_warnings(vice_shader_compute_reflection_tests)
  add_test(
    NAME vice_shader_compute_reflection_tests
    COMMAND vice_shader_compute_reflection_tests
  )
endif()
