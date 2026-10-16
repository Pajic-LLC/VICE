add_library(
  vice_rigid_material_cooker_lib
  STATIC
    src/rigid_material_cooker.cpp
)
add_library(VICE::RigidMaterialCooker ALIAS vice_rigid_material_cooker_lib)

target_include_directories(
  vice_rigid_material_cooker_lib
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(vice_rigid_material_cooker_lib PUBLIC VICE::RigidBody)
target_compile_features(vice_rigid_material_cooker_lib PUBLIC cxx_std_23)
vice_enable_warnings(vice_rigid_material_cooker_lib)

add_executable(vice_rigid_material_cooker src/main.cpp)
target_link_libraries(vice_rigid_material_cooker PRIVATE VICE::RigidMaterialCooker)
target_compile_features(vice_rigid_material_cooker PRIVATE cxx_std_23)
vice_enable_warnings(vice_rigid_material_cooker)
