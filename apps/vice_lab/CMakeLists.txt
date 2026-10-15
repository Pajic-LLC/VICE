add_executable(vice_lab main.cpp)

target_link_libraries(vice_lab PRIVATE VICE::Core VICE::Jobs VICE::Platform VICE::Render VICE::World)
target_compile_features(vice_lab PRIVATE cxx_std_23)
target_compile_definitions(
  vice_lab
  PRIVATE
    VICE_LAB_COOKED_SHADER_ROOT="${CMAKE_BINARY_DIR}/cooked"
)
vice_enable_warnings(vice_lab)
