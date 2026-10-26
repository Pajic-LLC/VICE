add_executable(
  vice_player
  app.cpp
)

target_link_libraries(
  vice_player
  PRIVATE
    VICE::Runtime
    VICE::RuntimeDevSession
    VICE::RuntimeDiagnostics
)

target_compile_features(vice_player PRIVATE cxx_std_23)
vice_enable_warnings(vice_player)

if(VICE_BUILD_TOOLS AND TARGET vice_project_workflow AND NOT TARGET vice_dev_cli)
  # Register the structured developer CLI after the real staged runtime host
  # exists so project run and runtime inspection share the same executable.
  add_subdirectory(
    "${PROJECT_SOURCE_DIR}/tools/vice_cli"
    "${CMAKE_BINARY_DIR}/tools/vice_cli"
  )
  add_dependencies(vice_cli vice_player)
endif()
