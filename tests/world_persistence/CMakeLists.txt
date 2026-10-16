if(TARGET vice_world_cooker_lib)
  add_executable(
    vice_world_snapshot_file_tests
    ../world_snapshot_file_tests.cpp
  )
  target_link_libraries(
    vice_world_snapshot_file_tests
    PRIVATE
      VICE::World
      VICE::WorldCooker
  )
  target_compile_features(vice_world_snapshot_file_tests PRIVATE cxx_std_23)
  vice_enable_warnings(vice_world_snapshot_file_tests)
  add_test(NAME vice_world_snapshot_file_tests COMMAND vice_world_snapshot_file_tests)
endif()
