cmake_minimum_required(VERSION 3.28)

if(NOT DEFINED VICE_TEST_BUILD_DIR OR NOT DEFINED VICE_TEST_CONFIG)
  message(FATAL_ERROR "Set VICE_TEST_BUILD_DIR and VICE_TEST_CONFIG.")
endif()
include("${VICE_TEST_BUILD_DIR}/vice_expected_tests.cmake")
if(NOT VICE_EXPECTED_TESTS)
  message(FATAL_ERROR "No VICE test executables were configured.")
endif()

execute_process(
  COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${VICE_TEST_BUILD_DIR}"
    -C "${VICE_TEST_CONFIG}" --show-only=json-v1
  RESULT_VARIABLE result
  OUTPUT_VARIABLE manifest
  ERROR_VARIABLE diagnostics
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "CTest discovery failed: ${diagnostics}")
endif()

string(JSON count LENGTH "${manifest}" tests)
set(discovered)
if(count GREATER 0)
  math(EXPR last "${count} - 1")
  foreach(index RANGE 0 ${last})
    string(JSON name GET "${manifest}" tests ${index} name)
    list(APPEND discovered "${name}")
  endforeach()
endif()
foreach(expected IN LISTS VICE_EXPECTED_TESTS)
  if(NOT expected IN_LIST discovered)
    message(FATAL_ERROR "CTest did not discover configured VICE test ${expected}.")
  endif()
endforeach()
list(LENGTH VICE_EXPECTED_TESTS expected_count)
message(STATUS "CTest discovered all ${expected_count} configured VICE test executables.")
