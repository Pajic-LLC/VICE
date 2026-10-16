add_library(
  vice_collision_import_lib
  STATIC
    src/collision_import.cpp
)
add_library(VICE::CollisionImport ALIAS vice_collision_import_lib)

target_include_directories(
  vice_collision_import_lib
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(
  vice_collision_import_lib
  PUBLIC
    VICE::CollisionCooker
    VICE::Math
)

target_compile_features(vice_collision_import_lib PUBLIC cxx_std_23)
vice_enable_warnings(vice_collision_import_lib)
