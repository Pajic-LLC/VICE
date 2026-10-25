add_executable(
  vice_runtime_reflection_surface_guide_tests
  runtime_reflection_surface_guide_tests.cpp
)
target_link_libraries(
  vice_runtime_reflection_surface_guide_tests
  PRIVATE
    VICE::Runtime
)
target_compile_features(vice_runtime_reflection_surface_guide_tests PRIVATE cxx_std_23)
target_compile_definitions(
  vice_runtime_reflection_surface_guide_tests
  PRIVATE
    VICE_REFLECTION_SURFACE_GUIDE_SHADER_SOURCE="${CMAKE_SOURCE_DIR}/assets/shaders/vice_reflection_surface_guide.hlsl"
    VICE_REFLECTION_SURFACE_GUIDE_CPP_SOURCE="${CMAKE_SOURCE_DIR}/engine/runtime/src/reflection_surface_guide.cpp"
    VICE_REFLECTION_SURFACE_GUIDE_BRIDGE_SOURCE="${CMAKE_SOURCE_DIR}/engine/runtime/src/reflection_surface_guide_bridge.hpp"
    VICE_RUNTIME_SCENE_LEGACY_SOURCE="${CMAKE_SOURCE_DIR}/engine/runtime/src/scene_legacy.inc"
)
vice_enable_warnings(vice_runtime_reflection_surface_guide_tests)
add_test(
  NAME vice_runtime_reflection_surface_guide_tests
  COMMAND vice_runtime_reflection_surface_guide_tests
)
