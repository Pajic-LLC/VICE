add_executable(
  vice_virtual_geometry_proxy_tests
  virtual_geometry_proxy_tests.cpp
)
target_link_libraries(vice_virtual_geometry_proxy_tests PRIVATE VICE::Render)
target_compile_features(vice_virtual_geometry_proxy_tests PRIVATE cxx_std_23)
vice_enable_warnings(vice_virtual_geometry_proxy_tests)
add_test(
  NAME vice_virtual_geometry_proxy_tests
  COMMAND vice_virtual_geometry_proxy_tests
)

if(VICE_BUILD_TOOLS)
  target_sources(
    vice_mesh_asset_tests
    PRIVATE
      mesh_asset_v5_cook_compat.cpp
  )
  target_sources(
    vice_mesh_asset_v5_tests
    PRIVATE
      mesh_asset_v5_cook_compat.cpp
  )
  set_source_files_properties(
    "${CMAKE_SOURCE_DIR}/tests/mesh_asset_tests.cpp"
    TARGET_DIRECTORY vice_mesh_asset_tests
    PROPERTIES COMPILE_DEFINITIONS "cook=cook_v5_fixture"
  )
  set_source_files_properties(
    "${CMAKE_SOURCE_DIR}/tests/mesh_asset_v5_tests.cpp"
    TARGET_DIRECTORY vice_mesh_asset_v5_tests
    PROPERTIES COMPILE_DEFINITIONS "cook=cook_v5_fixture"
  )

  add_executable(
    vice_mesh_asset_v6_tests
    mesh_asset_v6_tests.cpp
  )
  target_link_libraries(vice_mesh_asset_v6_tests PRIVATE VICE::MeshCooker VICE::Render)
  target_compile_features(vice_mesh_asset_v6_tests PRIVATE cxx_std_23)
  vice_enable_warnings(vice_mesh_asset_v6_tests)
  add_test(
    NAME vice_mesh_asset_v6_tests
    COMMAND vice_mesh_asset_v6_tests
  )
endif()
