add_executable(vice_core_tests core_tests.cpp)

target_link_libraries(vice_core_tests PRIVATE VICE::Core)
target_compile_features(vice_core_tests PRIVATE cxx_std_23)
vice_enable_warnings(vice_core_tests)

add_test(NAME vice_core_tests COMMAND vice_core_tests)
