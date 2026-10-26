add_library(
  vice_world_cooker_lib
  STATIC
    src/world_authoring.cpp
    src/world_cooker.cpp
    src/world_source_serializer.cpp
    src/world_source_transaction.cpp
)
add_library(VICE::WorldCooker ALIAS vice_world_cooker_lib)

target_include_directories(
  vice_world_cooker_lib
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(
  vice_world_cooker_lib
  PUBLIC
    VICE::World
  PRIVATE
    VICE::Platform
)
target_compile_features(vice_world_cooker_lib PUBLIC cxx_std_23)
vice_enable_warnings(vice_world_cooker_lib)

if(VICE_BUILD_TOOLS)
  add_executable(vice_world_cooker src/main.cpp)
  target_link_libraries(vice_world_cooker PRIVATE VICE::WorldCooker VICE::World)
  target_compile_features(vice_world_cooker PRIVATE cxx_std_23)
  vice_enable_warnings(vice_world_cooker)
endif()

if(VICE_BUILD_TESTS)
  add_executable(
    vice_world_authoring_tests
    tests/world_authoring_tests.cpp
  )
  target_link_libraries(
    vice_world_authoring_tests
    PRIVATE
      VICE::WorldCooker
      VICE::World
  )
  target_compile_features(vice_world_authoring_tests PRIVATE cxx_std_23)
  vice_enable_warnings(vice_world_authoring_tests)
  add_test(
    NAME vice_world_authoring_tests
    COMMAND vice_world_authoring_tests
  )

  add_executable(
    vice_world_source_transaction_tests
    tests/world_source_transaction_tests.cpp
  )
  target_link_libraries(
    vice_world_source_transaction_tests
    PRIVATE
      VICE::WorldCooker
      VICE::World
  )
  target_compile_features(vice_world_source_transaction_tests PRIVATE cxx_std_23)
  vice_enable_warnings(vice_world_source_transaction_tests)
  add_test(
    NAME vice_world_source_transaction_tests
    COMMAND vice_world_source_transaction_tests
  )
endif()
