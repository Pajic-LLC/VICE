function(vice_add_runtime_module_fixture target source)
  add_library(${target} MODULE ${source})
  target_include_directories(
    ${target}
    PRIVATE
      "${PROJECT_SOURCE_DIR}/engine/runtime/include"
  )
  target_compile_features(${target} PRIVATE cxx_std_23)
  set_target_properties(
    ${target}
    PROPERTIES
      PREFIX ""
      CXX_VISIBILITY_PRESET hidden
      VISIBILITY_INLINES_HIDDEN YES
  )
  if(APPLE)
    set_target_properties(${target} PROPERTIES SUFFIX ".dylib")
  endif()
  vice_enable_warnings(${target})
endfunction()

vice_add_runtime_module_fixture(vice_runtime_test_module test_game_module.cpp)
vice_add_runtime_module_fixture(vice_runtime_bad_module test_bad_game_module.cpp)
vice_add_runtime_module_fixture(vice_runtime_incompatible_module test_incompatible_game_module.cpp)

add_executable(
  vice_runtime_tests
  ../runtime_tests.cpp
)

target_link_libraries(
  vice_runtime_tests
  PRIVATE
    VICE::Runtime
)

add_dependencies(
  vice_runtime_tests
  vice_runtime_test_module
  vice_runtime_bad_module
  vice_runtime_incompatible_module
)

target_compile_definitions(
  vice_runtime_tests
  PRIVATE
    VICE_RUNTIME_TEST_MODULE="$<TARGET_FILE:vice_runtime_test_module>"
    VICE_RUNTIME_BAD_MODULE="$<TARGET_FILE:vice_runtime_bad_module>"
    VICE_RUNTIME_INCOMPATIBLE_MODULE="$<TARGET_FILE:vice_runtime_incompatible_module>"
)

target_compile_features(vice_runtime_tests PRIVATE cxx_std_23)
vice_enable_warnings(vice_runtime_tests)
add_test(NAME vice_runtime_tests COMMAND vice_runtime_tests)

add_executable(
  vice_runtime_render_material_tests
  ../runtime_render_material_tests.cpp
)
target_link_libraries(
  vice_runtime_render_material_tests
  PRIVATE
    VICE::Runtime
)
target_compile_features(vice_runtime_render_material_tests PRIVATE cxx_std_23)
vice_enable_warnings(vice_runtime_render_material_tests)
add_test(NAME vice_runtime_render_material_tests COMMAND vice_runtime_render_material_tests)

add_executable(vice_runtime_light_tests ../runtime_light_tests.cpp)
target_link_libraries(vice_runtime_light_tests PRIVATE VICE::Runtime)
target_compile_features(vice_runtime_light_tests PRIVATE cxx_std_23)
vice_enable_warnings(vice_runtime_light_tests)
add_test(NAME vice_runtime_light_tests COMMAND vice_runtime_light_tests)

add_executable(
  vice_runtime_texture_tests
  ../runtime_texture_tests.cpp
)
target_link_libraries(
  vice_runtime_texture_tests
  PRIVATE
    VICE::Runtime
)
target_compile_features(vice_runtime_texture_tests PRIVATE cxx_std_23)
vice_enable_warnings(vice_runtime_texture_tests)
add_test(NAME vice_runtime_texture_tests COMMAND vice_runtime_texture_tests)

add_executable(
  vice_runtime_visibility_tests
  ../runtime_visibility_tests.cpp
)
target_link_libraries(
  vice_runtime_visibility_tests
  PRIVATE
    VICE::Runtime
)
target_compile_features(vice_runtime_visibility_tests PRIVATE cxx_std_23)
vice_enable_warnings(vice_runtime_visibility_tests)
add_test(NAME vice_runtime_visibility_tests COMMAND vice_runtime_visibility_tests)

if(TARGET vice_mesh_cooker_lib)
  add_executable(
    vice_runtime_scene_tests
    ../runtime_scene_tests.cpp
  )
  target_link_libraries(
    vice_runtime_scene_tests
    PRIVATE
      VICE::MeshCooker
      VICE::Runtime
  )
  target_compile_features(vice_runtime_scene_tests PRIVATE cxx_std_23)
  vice_enable_warnings(vice_runtime_scene_tests)
  add_test(NAME vice_runtime_scene_tests COMMAND vice_runtime_scene_tests)
endif()
