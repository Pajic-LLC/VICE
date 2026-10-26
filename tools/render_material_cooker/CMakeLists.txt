add_library(
  vice_render_material_cooker_lib
  STATIC
    src/render_material_cooker.cpp
    src/project_render_material_cook.cpp
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
    VICE::Project
    VICE::Render
    VICE::World
  PRIVATE
    VICE::ShaderCooker
)
target_compile_features(vice_render_material_cooker_lib PUBLIC cxx_std_23)

include(ViceShaders)
vice_find_dxc(_vice_project_material_dxc)
vice_find_metal_shader_converter(_vice_project_material_metal_shader_converter)
if(_vice_project_material_dxc)
  target_compile_definitions(
    vice_render_material_cooker_lib
    PRIVATE
      VICE_PROJECT_DXC_EXECUTABLE="${_vice_project_material_dxc}"
  )
endif()
if(_vice_project_material_metal_shader_converter)
  target_compile_definitions(
    vice_render_material_cooker_lib
    PRIVATE
      VICE_PROJECT_METAL_SHADER_CONVERTER_EXECUTABLE="${_vice_project_material_metal_shader_converter}"
  )
endif()

vice_enable_warnings(vice_render_material_cooker_lib)
