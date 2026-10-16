add_library(
  vice_collision_cooker_lib
  STATIC
    src/collision_cooker.cpp
)
add_library(VICE::CollisionCooker ALIAS vice_collision_cooker_lib)

target_include_directories(
  vice_collision_cooker_lib
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(
  vice_collision_cooker_lib
  PUBLIC
    VICE::RigidBody
)

target_compile_features(vice_collision_cooker_lib PUBLIC cxx_std_23)
vice_enable_warnings(vice_collision_cooker_lib)

add_executable(vice_collision_cooker src/main.cpp)
target_link_libraries(vice_collision_cooker PRIVATE VICE::CollisionCooker)
target_compile_features(vice_collision_cooker PRIVATE cxx_std_23)
vice_enable_warnings(vice_collision_cooker)
