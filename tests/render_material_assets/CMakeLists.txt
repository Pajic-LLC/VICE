if(TARGET vice_render_material_cooker_lib)
  add_executable(
    vice_render_material_asset_tests
    ../render_material_asset_tests.cpp
  )
  target_link_libraries(
    vice_render_material_asset_tests
    PRIVATE
      VICE::Render
      VICE::RenderMaterialCooker
  )
  target_compile_features(vice_render_material_asset_tests PRIVATE cxx_std_23)
  vice_enable_warnings(vice_render_material_asset_tests)
  add_test(NAME vice_render_material_asset_tests COMMAND vice_render_material_asset_tests)
endif()
