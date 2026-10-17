add_executable(
  vice_render_dynamic_state_tests
  render_dynamic_state_tests.cpp
)

target_link_libraries(
  vice_render_dynamic_state_tests
  PRIVATE VICE::Render
)
target_compile_features(vice_render_dynamic_state_tests PRIVATE cxx_std_23)
vice_enable_warnings(vice_render_dynamic_state_tests)
add_test(NAME vice_render_dynamic_state_tests COMMAND vice_render_dynamic_state_tests)
