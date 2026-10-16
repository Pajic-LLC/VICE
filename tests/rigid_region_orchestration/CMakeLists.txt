if(VICE_ENABLE_JOLT_RIGID_BODY)
  add_executable(
    vice_rigid_body_box_orchestration_tests
    ../rigid_body_box_orchestration_tests.cpp
  )
  target_link_libraries(
    vice_rigid_body_box_orchestration_tests
    PRIVATE VICE::RigidBodyWorld
  )
  target_compile_features(vice_rigid_body_box_orchestration_tests PRIVATE cxx_std_23)
  vice_enable_warnings(vice_rigid_body_box_orchestration_tests)
  add_test(
    NAME vice_rigid_body_box_orchestration_tests
    COMMAND vice_rigid_body_box_orchestration_tests
  )
endif()
