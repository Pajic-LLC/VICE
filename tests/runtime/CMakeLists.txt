add_executable(
  vice_runtime_tests
  ../runtime_tests.cpp
)

target_link_libraries(
  vice_runtime_tests
  PRIVATE
    VICE::Runtime
)

target_compile_features(vice_runtime_tests PRIVATE cxx_std_23)
vice_enable_warnings(vice_runtime_tests)
add_test(NAME vice_runtime_tests COMMAND vice_runtime_tests)
