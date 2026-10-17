if(TARGET vice_editor_state)
  add_executable(
    vice_editor_state_tests
    ../editor_state_tests.cpp
  )
  target_link_libraries(vice_editor_state_tests PRIVATE VICE::EditorState)
  target_compile_features(vice_editor_state_tests PRIVATE cxx_std_23)
  vice_enable_warnings(vice_editor_state_tests)
  add_test(NAME vice_editor_state_tests COMMAND vice_editor_state_tests)

  add_executable(
    vice_editor_authoring_tests
    ../editor_authoring_tests.cpp
  )
  target_link_libraries(vice_editor_authoring_tests PRIVATE VICE::EditorState)
  target_compile_features(vice_editor_authoring_tests PRIVATE cxx_std_23)
  vice_enable_warnings(vice_editor_authoring_tests)
  add_test(NAME vice_editor_authoring_tests COMMAND vice_editor_authoring_tests)

  add_executable(
    vice_editor_project_session_tests
    ../editor_project_session_tests.cpp
  )
  target_link_libraries(vice_editor_project_session_tests PRIVATE VICE::EditorState)
  target_compile_features(vice_editor_project_session_tests PRIVATE cxx_std_23)
  vice_enable_warnings(vice_editor_project_session_tests)
  add_test(NAME vice_editor_project_session_tests COMMAND vice_editor_project_session_tests)

  add_executable(
    vice_editor_project_settings_tests
    ../editor_project_settings_tests.cpp
  )
  target_link_libraries(vice_editor_project_settings_tests PRIVATE VICE::EditorState)
  target_compile_features(vice_editor_project_settings_tests PRIVATE cxx_std_23)
  vice_enable_warnings(vice_editor_project_settings_tests)
  add_test(NAME vice_editor_project_settings_tests COMMAND vice_editor_project_settings_tests)
endif()

if(TARGET vice_editor_project_build)
  add_executable(
    vice_editor_project_build_tests
    ../editor_project_build_tests.cpp
  )
  target_link_libraries(vice_editor_project_build_tests PRIVATE VICE::EditorProjectBuild)
  target_compile_features(vice_editor_project_build_tests PRIVATE cxx_std_23)
  vice_enable_warnings(vice_editor_project_build_tests)
  add_test(NAME vice_editor_project_build_tests COMMAND vice_editor_project_build_tests)
endif()

if(TARGET vice_editor_asset_index AND TARGET vice_mesh_cooker_lib)
  add_executable(
    vice_editor_asset_index_tests
    ../editor_asset_index_tests.cpp
  )
  target_link_libraries(
    vice_editor_asset_index_tests
    PRIVATE
      VICE::EditorAssetIndex
      VICE::MeshCooker
  )
  target_compile_features(vice_editor_asset_index_tests PRIVATE cxx_std_23)
  vice_enable_warnings(vice_editor_asset_index_tests)
  add_test(NAME vice_editor_asset_index_tests COMMAND vice_editor_asset_index_tests)

  add_executable(
    vice_editor_mesh_picking_tests
    ../editor_mesh_picking_tests.cpp
  )
  target_link_libraries(
    vice_editor_mesh_picking_tests
    PRIVATE
      VICE::EditorAssetIndex
      VICE::MeshCooker
  )
  target_compile_features(vice_editor_mesh_picking_tests PRIVATE cxx_std_23)
  vice_enable_warnings(vice_editor_mesh_picking_tests)
  add_test(NAME vice_editor_mesh_picking_tests COMMAND vice_editor_mesh_picking_tests)
endif()

if(TARGET vice_editor_preview)
  add_executable(
    vice_editor_preview_runtime_tests
    ../editor_preview_runtime_tests.cpp
  )
  target_link_libraries(vice_editor_preview_runtime_tests PRIVATE VICE::EditorPreview)
  target_compile_features(vice_editor_preview_runtime_tests PRIVATE cxx_std_23)
  vice_enable_warnings(vice_editor_preview_runtime_tests)
  add_test(NAME vice_editor_preview_runtime_tests COMMAND vice_editor_preview_runtime_tests)
endif()

if(TARGET vice_editor_profiler)
  add_executable(
    vice_editor_profiler_tests
    ../editor_profiler_tests.cpp
  )
  target_link_libraries(vice_editor_profiler_tests PRIVATE VICE::EditorProfiler)
  target_compile_features(vice_editor_profiler_tests PRIVATE cxx_std_23)
  target_compile_options(
    vice_editor_profiler_tests
    PRIVATE
      $<$<CXX_COMPILER_ID:MSVC>:/UNDEBUG>
      $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-UNDEBUG>
  )
  vice_enable_warnings(vice_editor_profiler_tests)
  add_test(NAME vice_editor_profiler_tests COMMAND vice_editor_profiler_tests)
endif()

if(TARGET vice_editor_ui)
  add_executable(
    vice_editor_ui_tests
    ../editor_ui_tests.cpp
  )
  target_link_libraries(vice_editor_ui_tests PRIVATE VICE::EditorUi)
  target_compile_features(vice_editor_ui_tests PRIVATE cxx_std_23)
  vice_enable_warnings(vice_editor_ui_tests)
  add_test(NAME vice_editor_ui_tests COMMAND vice_editor_ui_tests)

  add_executable(
    vice_editor_physics_debug_ui_tests
    ../editor_physics_debug_ui_tests.cpp
  )
  target_link_libraries(vice_editor_physics_debug_ui_tests PRIVATE VICE::EditorUi)
  target_compile_features(vice_editor_physics_debug_ui_tests PRIVATE cxx_std_23)
  vice_enable_warnings(vice_editor_physics_debug_ui_tests)
  add_test(NAME vice_editor_physics_debug_ui_tests COMMAND vice_editor_physics_debug_ui_tests)
endif()
