add_library(
  vice_texture_cooker_lib STATIC
  src/texture_cooker.cpp
)
add_library(VICE::TextureCooker ALIAS vice_texture_cooker_lib)

target_include_directories(
  vice_texture_cooker_lib
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(
  vice_texture_cooker_lib
  PUBLIC VICE::Render
  PRIVATE VICE::World
)
target_compile_features(vice_texture_cooker_lib PUBLIC cxx_std_23)
vice_enable_warnings(vice_texture_cooker_lib)

if(VICE_BUILD_TOOLS)
  add_executable(vice_texture_cooker src/main.cpp)
  target_link_libraries(vice_texture_cooker PRIVATE VICE::TextureCooker)
  target_compile_features(vice_texture_cooker PRIVATE cxx_std_23)
  vice_enable_warnings(vice_texture_cooker)
endif()
