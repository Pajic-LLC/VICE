add_library(
  vice_world
  STATIC
    src/persistent_id.cpp
    src/primitives.cpp
    src/world.cpp
)

add_library(VICE::World ALIAS vice_world)

target_include_directories(
  vice_world
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(vice_world PUBLIC VICE::Core)
target_compile_features(vice_world PUBLIC cxx_std_23)
vice_enable_warnings(vice_world)
