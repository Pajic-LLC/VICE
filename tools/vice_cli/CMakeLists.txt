add_library(
  vice_dev_cli
  STATIC
    src/cli.cpp
    src/runtime_debug.cpp
    src/runtime_profile.cpp
    src/world_edit.cpp
)
add_library(VICE::DevCLI ALIAS vice_dev_cli)

target_include_directories(
  vice_dev_cli
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(
  vice_dev_cli
  PUBLIC
    VICE::Project
    VICE::ProjectWorkflow
    VICE::World
    VICE::WorldCooker
  PRIVATE
    VICE::Platform
    VICE::RuntimeDevSession
)
if(VICE_BUILD_PLAYER)
  target_compile_definitions(
    vice_dev_cli
    PRIVATE
      VICE_DEV_RUNTIME_HOST_EXECUTABLE="$<TARGET_FILE:vice_player>"
  )
endif()
target_compile_features(vice_dev_cli PUBLIC cxx_std_23)
vice_enable_warnings(vice_dev_cli)

add_executable(vice_cli src/main.cpp)
target_link_libraries(vice_cli PRIVATE VICE::DevCLI)
target_compile_features(vice_cli PRIVATE cxx_std_23)
vice_enable_warnings(vice_cli)
set_target_properties(vice_cli PROPERTIES OUTPUT_NAME vice)

if(VICE_BUILD_TESTS)
  add_executable(vice_world_edit_cli_tests tests/world_edit_tests.cpp)
  target_link_libraries(vice_world_edit_cli_tests PRIVATE VICE::DevCLI VICE::Platform)
  target_compile_features(vice_world_edit_cli_tests PRIVATE cxx_std_23)
  vice_enable_warnings(vice_world_edit_cli_tests)
  add_test(NAME vice_world_edit_cli_tests COMMAND vice_world_edit_cli_tests)

  add_executable(
    vice_runtime_debug_cli_tests
    tests/runtime_debug_tests.cpp
  )
  target_link_libraries(
    vice_runtime_debug_cli_tests
    PRIVATE
      VICE::DevCLI
      VICE::RuntimeDevSession
  )
  target_compile_features(vice_runtime_debug_cli_tests PRIVATE cxx_std_23)
  vice_enable_warnings(vice_runtime_debug_cli_tests)
  add_test(
    NAME vice_runtime_debug_cli_tests
    COMMAND vice_runtime_debug_cli_tests
  )

  add_executable(
    vice_runtime_profile_cli_tests
    tests/runtime_profile_tests.cpp
  )
  target_link_libraries(
    vice_runtime_profile_cli_tests
    PRIVATE
      VICE::DevCLI
      VICE::RuntimeDevSession
  )
  target_compile_features(vice_runtime_profile_cli_tests PRIVATE cxx_std_23)
  vice_enable_warnings(vice_runtime_profile_cli_tests)
  add_test(
    NAME vice_runtime_profile_cli_tests
    COMMAND vice_runtime_profile_cli_tests
  )
endif()
