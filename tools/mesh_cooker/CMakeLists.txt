add_library(
  vice_mesh_cooker_lib
  STATIC
    src/mesh_cooker.cpp
)
add_library(VICE::MeshCooker ALIAS vice_mesh_cooker_lib)

target_include_directories(
  vice_mesh_cooker_lib
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(vice_mesh_cooker_lib PUBLIC VICE::Render)
target_compile_features(vice_mesh_cooker_lib PUBLIC cxx_std_23)
vice_enable_warnings(vice_mesh_cooker_lib)

add_executable(vice_mesh_cooker src/main.cpp)
target_link_libraries(vice_mesh_cooker PRIVATE VICE::MeshCooker)
if(TARGET vice_collision_import_lib)
  target_link_libraries(
    vice_mesh_cooker
    PRIVATE
      VICE::CollisionImport
      VICE::CollisionCooker
  )
  target_compile_definitions(vice_mesh_cooker PRIVATE VICE_MESH_COOKER_COLLISION=1)
else()
  target_compile_definitions(vice_mesh_cooker PRIVATE VICE_MESH_COOKER_COLLISION=0)
endif()
target_compile_features(vice_mesh_cooker PRIVATE cxx_std_23)
vice_enable_warnings(vice_mesh_cooker)
