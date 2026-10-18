if(TARGET vice_texture_cooker_lib)
  add_executable(
    vice_texture_asset_tests
    ../texture_asset_tests.cpp
  )
  target_link_libraries(
    vice_texture_asset_tests
    PRIVATE
      VICE::Render
      VICE::TextureCooker
  )
  target_compile_features(vice_texture_asset_tests PRIVATE cxx_std_23)
  vice_enable_warnings(vice_texture_asset_tests)
  add_test(NAME vice_texture_asset_tests COMMAND vice_texture_asset_tests)
endif()
