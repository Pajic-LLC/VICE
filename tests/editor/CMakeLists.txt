if(TARGET vice_editor_state)
  add_executable(
    vice_editor_state_tests
    ../editor_state_tests.cpp
  )
  target_link_libraries(vice_editor_state_tests PRIVATE VICE::EditorState)
  target_compile_features(vice_editor_state_tests PRIVATE cxx_std_23)
  vice_enable_warnings(vice_editor_state_tests)
  add_test(NAME vice_editor_state_tests COMMAND vice_editor_state_tests)
endif()
