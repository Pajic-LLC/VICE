add_library(vice_dade_test_provider MODULE test_dade_provider.cpp)

target_include_directories(
  vice_dade_test_provider
  PRIVATE
    "${PROJECT_SOURCE_DIR}/engine/runtime/include"
)

target_compile_features(vice_dade_test_provider PRIVATE cxx_std_23)
set_target_properties(
  vice_dade_test_provider
  PROPERTIES
    PREFIX ""
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
)
if(APPLE)
  set_target_properties(vice_dade_test_provider PROPERTIES SUFFIX ".dylib")
endif()
vice_enable_warnings(vice_dade_test_provider)

add_executable(vice_dade_tests dade_tests.cpp)
target_link_libraries(vice_dade_tests PRIVATE VICE::Runtime)
add_dependencies(vice_dade_tests vice_dade_test_provider)
target_compile_definitions(
  vice_dade_tests
  PRIVATE
    VICE_DADE_TEST_PROVIDER="$<TARGET_FILE:vice_dade_test_provider>"
)
target_compile_features(vice_dade_tests PRIVATE cxx_std_23)
vice_enable_warnings(vice_dade_tests)
add_test(NAME vice_dade_tests COMMAND vice_dade_tests)
