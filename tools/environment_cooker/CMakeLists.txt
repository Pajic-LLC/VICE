add_library(
  vice_environment_cooker_lib STATIC
  src/environment_cooker.cpp
  src/radiance_hdr.cpp
)
add_library(VICE::EnvironmentCooker ALIAS vice_environment_cooker_lib)

target_include_directories(
  vice_environment_cooker_lib
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(
  vice_environment_cooker_lib
  PUBLIC
    VICE::Render
    VICE::TextureCooker
  PRIVATE
    VICE::World
)
target_compile_features(vice_environment_cooker_lib PUBLIC cxx_std_23)
vice_enable_warnings(vice_environment_cooker_lib)
