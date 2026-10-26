add_library(
  vice_world_cooker_lib
  STATIC
    src/world_cooker.cpp
    src/world_source_serializer.cpp
)
add_library(VICE::WorldCooker ALIAS vice_world_cooker_lib)

target_include_directories(
  vice_world_cooker_lib
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(vice_world_cooker_lib PUBLIC VICE::World)
target_compile_features(vice_world_cooker_lib PUBLIC cxx_std_23)
vice_enable_warnings(vice_world_cooker_lib)

if(VICE_BUILD_TOOLS)
  add_executable(vice_world_cooker src/main.cpp)
  target_link_libraries(vice_world_cooker PRIVATE VICE::WorldCooker VICE::World)
  target_compile_features(vice_world_cooker PRIVATE cxx_std_23)
  vice_enable_warnings(vice_world_cooker)

  # The structured developer CLI consumes the authoritative World cooker contract.
  # Register it here so editor-only builds do not pull in the developer command surface.
  add_subdirectory(
    "${PROJECT_SOURCE_DIR}/tools/vice_cli"
    "${CMAKE_BINARY_DIR}/tools/vice_cli"
  )
endif()
