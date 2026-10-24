add_executable(
  vice_ray_tracing_rhi_tests
  ray_tracing_rhi_tests.cpp
)
target_link_libraries(vice_ray_tracing_rhi_tests PRIVATE VICE::Render)
target_compile_features(vice_ray_tracing_rhi_tests PRIVATE cxx_std_23)
vice_enable_warnings(vice_ray_tracing_rhi_tests)
add_test(
  NAME vice_ray_tracing_rhi_tests
  COMMAND vice_ray_tracing_rhi_tests
)

if(VICE_BUILD_TOOLS)
  include(ViceShaders)
  vice_find_dxc(_vice_inline_ray_query_dxc)
  vice_find_metal_shader_converter(_vice_inline_ray_query_metal_converter)

  set(_vice_inline_ray_query_target "")
  if(WIN32 AND _vice_inline_ray_query_dxc)
    set(_vice_inline_ray_query_target d3d12)
  elseif(APPLE AND _vice_inline_ray_query_dxc AND _vice_inline_ray_query_metal_converter)
    set(_vice_inline_ray_query_target metal)
  endif()

  if(_vice_inline_ray_query_target)
    set(
      _vice_inline_ray_query_output
      "${CMAKE_BINARY_DIR}/cooked/vice_inline_ray_query_smoke"
    )
    vice_add_cooked_shader(
      NAME vice_inline_ray_query_smoke_shader
      INPUT "${PROJECT_SOURCE_DIR}/assets/shaders/vice_inline_ray_query_smoke.hlsl"
      OUTPUT "${_vice_inline_ray_query_output}"
      ENTRY vice_inline_ray_query_cs
      STAGE compute
      TARGET "${_vice_inline_ray_query_target}"
      SHADER_MODEL 6.6
      DXC "${_vice_inline_ray_query_dxc}"
      METAL_SHADER_CONVERTER "${_vice_inline_ray_query_metal_converter}"
    )

    add_executable(
      vice_inline_ray_query_fixture_tests
      inline_ray_query_fixture_tests.cpp
    )
    target_link_libraries(vice_inline_ray_query_fixture_tests PRIVATE VICE::Render)
    target_compile_features(vice_inline_ray_query_fixture_tests PRIVATE cxx_std_23)
    target_compile_definitions(
      vice_inline_ray_query_fixture_tests
      PRIVATE
        VICE_INLINE_RAY_QUERY_MANIFEST="${_vice_inline_ray_query_output}/shader.vice.json"
    )
    vice_enable_warnings(vice_inline_ray_query_fixture_tests)
    add_dependencies(
      vice_inline_ray_query_fixture_tests
      vice_inline_ray_query_smoke_shader
    )
    add_test(
      NAME vice_inline_ray_query_fixture_tests
      COMMAND vice_inline_ray_query_fixture_tests
    )
  endif()
endif()
