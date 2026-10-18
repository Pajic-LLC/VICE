add_library(
  vice_render_material_cooker_lib
  STATIC
    src/render_material_cooker.cpp
)
add_library(VICE::RenderMaterialCooker ALIAS vice_render_material_cooker_lib)

target_include_directories(
  vice_render_material_cooker_lib
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(
  vice_render_material_cooker_lib
  PUBLIC
    VICE::Render
  PRIVATE
    VICE::World
)
target_compile_features(vice_render_material_cooker_lib PUBLIC cxx_std_23)
vice_enable_warnings(vice_render_material_cooker_lib)
