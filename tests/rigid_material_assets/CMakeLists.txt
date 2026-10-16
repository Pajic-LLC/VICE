if(VICE_ENABLE_JOLT_RIGID_BODY AND VICE_BUILD_TOOLS)
  add_executable(
    vice_rigid_material_asset_tests
    ../rigid_material_asset_tests.cpp
  )
  target_link_libraries(
    vice_rigid_material_asset_tests
    PRIVATE
      VICE::RigidBody
      VICE::RigidMaterialCooker
  )
  target_compile_features(vice_rigid_material_asset_tests PRIVATE cxx_std_23)
  vice_enable_warnings(vice_rigid_material_asset_tests)
  add_test(
    NAME vice_rigid_material_asset_tests
    COMMAND vice_rigid_material_asset_tests
  )
endif()
