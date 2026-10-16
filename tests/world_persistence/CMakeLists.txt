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

if(TARGET vice_rigid_body_world)
  add_executable(
    vice_world_persistence_runtime_tests
    runtime_binding_tests.cpp
  )
  target_link_libraries(
    vice_world_persistence_runtime_tests
    PRIVATE
      VICE::RigidBodyWorld
      VICE::World
  )
  target_compile_features(vice_world_persistence_runtime_tests PRIVATE cxx_std_23)
  vice_enable_warnings(vice_world_persistence_runtime_tests)
  add_test(
    NAME vice_world_persistence_runtime_tests
    COMMAND vice_world_persistence_runtime_tests
  )
endif()
