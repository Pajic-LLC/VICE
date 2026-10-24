cmake_minimum_required(VERSION 3.28)

project(
  VICE
  VERSION 0.1.0
  DESCRIPTION "Visual Interactive Computational Engine"
  LANGUAGES CXX
)

option(VICE_BUILD_TESTS "Build VICE tests" ON)
option(VICE_BUILD_LAB "Build the VICE Lab executable" ON)
option(VICE_BUILD_EDITOR "Build VICE editor targets" ON)
option(VICE_BUILD_PLAYER "Build VICE game runtime host" ON)
option(VICE_BUILD_TOOLS "Build VICE developer tools" ON)
option(VICE_ENABLE_JOLT_RIGID_BODY "Build the Jolt rigid body backend" ON)

set(VICE_DXC_EXECUTABLE "" CACHE FILEPATH "Path to the DXC executable used for shader cooking")
set(
  VICE_METAL_SHADER_CONVERTER_EXECUTABLE
  ""
  CACHE FILEPATH
  "Path to the Metal Shader Converter executable used for shader cooking"
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(ViceCompiler)

add_subdirectory(engine/core)
add_subdirectory(engine/math)
add_subdirectory(engine/jobs)
add_subdirectory(engine/platform)
add_subdirectory(engine/project)
add_subdirectory(engine/world)
add_subdirectory(engine/simulation)

if(VICE_ENABLE_JOLT_RIGID_BODY)
  add_subdirectory(engine/rigid_body)
endif()

add_subdirectory(engine/render)
add_subdirectory(engine/runtime)

if(VICE_BUILD_EDITOR OR VICE_BUILD_TOOLS)
  add_subdirectory(tools/world_cooker)
  add_subdirectory(tools/render_material_cooker)
  add_subdirectory(tools/shader_cooker)
  add_subdirectory(tools/texture_cooker)
  add_subdirectory(tools/environment_cooker)
endif()

if(VICE_BUILD_TOOLS)
  if(VICE_ENABLE_JOLT_RIGID_BODY)
    add_subdirectory(tools/collision_cooker)
    add_subdirectory(tools/collision_import)
    add_subdirectory(tools/rigid_material_cooker)
  endif()
  add_subdirectory(tools/mesh_cooker)
elseif(VICE_BUILD_EDITOR)
  add_subdirectory(tools/mesh_cooker)
endif()

if(TARGET vice_shader_cooker)
  include(ViceShaders)
  vice_find_dxc(_vice_runtime_ray_shadow_dxc)
  vice_find_metal_shader_converter(_vice_runtime_ray_shadow_metal_converter)

  set(_vice_runtime_ray_shadow_target "")
  if(WIN32 AND _vice_runtime_ray_shadow_dxc)
    set(_vice_runtime_ray_shadow_target d3d12)
  elseif(APPLE AND _vice_runtime_ray_shadow_dxc AND _vice_runtime_ray_shadow_metal_converter)
    set(_vice_runtime_ray_shadow_target metal)
  endif()

  if(_vice_runtime_ray_shadow_target)
    set(
      _vice_runtime_ray_shadow_output
      "${CMAKE_BINARY_DIR}/cooked/vice_runtime_directional_ray_shadow"
    )
    set(
      VICE_RUNTIME_DIRECTIONAL_RAY_SHADOW_MANIFEST_PATH
      "${_vice_runtime_ray_shadow_output}/shader.vice.json"
    )
    vice_add_cooked_shader(
      NAME vice_runtime_directional_ray_shadow_shader
      INPUT "${PROJECT_SOURCE_DIR}/assets/shaders/vice_directional_ray_shadow.hlsl"
      OUTPUT "${_vice_runtime_ray_shadow_output}"
      ENTRY vice_directional_ray_shadow_cs
      STAGE compute
      TARGET "${_vice_runtime_ray_shadow_target}"
      SHADER_MODEL 6.6
      DXC "${_vice_runtime_ray_shadow_dxc}"
      METAL_SHADER_CONVERTER "${_vice_runtime_ray_shadow_metal_converter}"
    )
    add_dependencies(vice_runtime vice_runtime_directional_ray_shadow_shader)
    target_compile_definitions(
      vice_runtime
      PRIVATE
        VICE_RUNTIME_DIRECTIONAL_RAY_SHADOW_MANIFEST="${VICE_RUNTIME_DIRECTIONAL_RAY_SHADOW_MANIFEST_PATH}"
    )

    set(
      _vice_runtime_ray_ambient_occlusion_output
      "${CMAKE_BINARY_DIR}/cooked/vice_runtime_ray_ambient_occlusion"
    )
    set(
      VICE_RUNTIME_RAY_AMBIENT_OCCLUSION_MANIFEST_PATH
      "${_vice_runtime_ray_ambient_occlusion_output}/shader.vice.json"
    )
    vice_add_cooked_shader(
      NAME vice_runtime_ray_ambient_occlusion_shader
      INPUT "${PROJECT_SOURCE_DIR}/assets/shaders/vice_ray_ambient_occlusion.hlsl"
      OUTPUT "${_vice_runtime_ray_ambient_occlusion_output}"
      ENTRY vice_ray_ambient_occlusion_cs
      STAGE compute
      TARGET "${_vice_runtime_ray_shadow_target}"
      SHADER_MODEL 6.6
      DXC "${_vice_runtime_ray_shadow_dxc}"
      METAL_SHADER_CONVERTER "${_vice_runtime_ray_shadow_metal_converter}"
    )
    add_dependencies(vice_runtime vice_runtime_ray_ambient_occlusion_shader)
    target_compile_definitions(
      vice_runtime
      PRIVATE
        VICE_RUNTIME_RAY_AMBIENT_OCCLUSION_MANIFEST="${VICE_RUNTIME_RAY_AMBIENT_OCCLUSION_MANIFEST_PATH}"
    )
  endif()
endif()

if(VICE_BUILD_LAB)
  add_subdirectory(apps/vice_lab)
endif()

if(VICE_BUILD_PLAYER)
  add_subdirectory(apps/vice_player)
endif()

if(VICE_BUILD_EDITOR)
  add_subdirectory(apps/vice_editor)
endif()

if(VICE_BUILD_TESTS)
  include(CTest)
  enable_testing()
  add_subdirectory(tests)
  add_subdirectory(tests/project)
  add_subdirectory(tests/runtime)
  add_subdirectory(tests/dade)
  add_subdirectory(tests/render_dynamic_state)
  add_subdirectory(tests/render_material_assets)
  add_subdirectory(tests/virtual_geometry_proxy)
  add_subdirectory(tests/texture_assets)
  add_subdirectory(tests/world_persistence)
  add_subdirectory(tests/rigid_region_orchestration)
  add_subdirectory(tests/rigid_material_assets)
  add_subdirectory(tests/ray_tracing_rhi)
  add_subdirectory(tests/runtime_ray_tracing_scene)
  add_subdirectory(tests/runtime_directional_ray_shadow)
  add_subdirectory(tests/runtime_ray_ambient_occlusion)
  if(APPLE)
    add_subdirectory(tests/metal_ray_tracing)
  endif()
  if(WIN32)
    add_subdirectory(tests/d3d12_ray_tracing)
  endif()
  if(VICE_BUILD_EDITOR)
    add_subdirectory(tests/editor)
  endif()
endif()
