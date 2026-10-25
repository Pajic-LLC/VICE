add_library(
  vice_dev_cli
  STATIC
    src/cli.cpp
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
    VICE::World
    VICE::WorldCooker
)
target_compile_features(vice_dev_cli PUBLIC cxx_std_23)
vice_enable_warnings(vice_dev_cli)

add_executable(vice_cli src/main.cpp)
target_link_libraries(vice_cli PRIVATE VICE::DevCLI)
target_compile_features(vice_cli PRIVATE cxx_std_23)
vice_enable_warnings(vice_cli)
set_target_properties(vice_cli PROPERTIES OUTPUT_NAME vice)
