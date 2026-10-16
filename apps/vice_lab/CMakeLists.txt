add_executable(vice_lab main.cpp)

target_link_libraries(
  vice_lab
  PRIVATE
    VICE::Core
    VICE::Math
    VICE::Jobs
    VICE::Platform
    VICE::Render
    VICE::World
    VICE::SimulationWorld
    VICE::RigidBody
    VICE::RigidBodyWorld
)
target_compile_features(vice_lab PRIVATE cxx_std_23)
target_compile_definitions(
  vice_lab
  PRIVATE
    VICE_LAB_COOKED_SHADER_ROOT="${CMAKE_BINARY_DIR}/cooked"
)
vice_enable_warnings(vice_lab)

if(VICE_BUILD_TOOLS)
  include(ViceShaders)
  vice_find_dxc(_vice_lab_dxc)
  vice_find_metal_shader_converter(_vice_lab_metal_shader_converter)

  set(_vice_lab_shader_target "")
  if(WIN32 AND _vice_lab_dxc)
    set(_vice_lab_shader_target d3d12)
  elseif(APPLE AND _vice_lab_dxc AND _vice_lab_metal_shader_converter)
    set(_vice_lab_shader_target metal)
  endif()

  if(_vice_lab_shader_target)
    set(_vice_lab_shader_source "${PROJECT_SOURCE_DIR}/assets/shaders/vice_lab_cube.hlsl")

    vice_add_cooked_shader(
      NAME vice_lab_cube_vs_shader
      INPUT "${_vice_lab_shader_source}"
      OUTPUT "${CMAKE_BINARY_DIR}/cooked/vice_lab_cube_vs"
      ENTRY vice_cube_vs
      STAGE vertex
      TARGET "${_vice_lab_shader_target}"
      DXC "${_vice_lab_dxc}"
      METAL_SHADER_CONVERTER "${_vice_lab_metal_shader_converter}"
    )

    vice_add_cooked_shader(
      NAME vice_lab_cube_fs_shader
      INPUT "${_vice_lab_shader_source}"
      OUTPUT "${CMAKE_BINARY_DIR}/cooked/vice_lab_cube_fs"
      ENTRY vice_cube_fs
      STAGE fragment
      TARGET "${_vice_lab_shader_target}"
      DXC "${_vice_lab_dxc}"
      METAL_SHADER_CONVERTER "${_vice_lab_metal_shader_converter}"
    )

    add_custom_target(
      vice_lab_shaders
      DEPENDS
        vice_lab_cube_vs_shader
        vice_lab_cube_fs_shader
    )
    add_dependencies(vice_lab vice_lab_shaders)

    message(STATUS "VICE Lab cooked shaders enabled for ${_vice_lab_shader_target}.")
  elseif(WIN32)
    message(STATUS "VICE Lab cooked shaders disabled because DXC was not found. Source fallback remains enabled.")
  elseif(APPLE)
    message(STATUS "VICE Lab cooked shaders disabled because DXC or Metal Shader Converter was not found. Source fallback remains enabled.")
  endif()

  if(TARGET vice_mesh_cooker AND TARGET vice_collision_cooker)
    set(_vice_lab_scene_source "${PROJECT_SOURCE_DIR}/assets/meshes/vice_lab_ramp.vmesh")
    set(_vice_lab_terrain_source "${PROJECT_SOURCE_DIR}/assets/collision/vice_lab_terrain.vcollision")
    set(_vice_lab_scene_mesh "${CMAKE_BINARY_DIR}/cooked/vice_lab_ramp.vmeshc")
    set(_vice_lab_scene_collision "${CMAKE_BINARY_DIR}/cooked/vice_lab_ramp.vcollisionc")
    set(_vice_lab_terrain_collision "${CMAKE_BINARY_DIR}/cooked/vice_lab_terrain.vcollisionc")

    add_custom_command(
      OUTPUT
        "${_vice_lab_scene_mesh}"
        "${_vice_lab_scene_collision}"
      COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/cooked"
      COMMAND
        $<TARGET_FILE:vice_mesh_cooker>
        --input "${_vice_lab_scene_source}"
        --output "${_vice_lab_scene_mesh}"
        --collision-output "${_vice_lab_scene_collision}"
      DEPENDS
        vice_mesh_cooker
        "${_vice_lab_scene_source}"
      VERBATIM
    )

    add_custom_command(
      OUTPUT "${_vice_lab_terrain_collision}"
      COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/cooked"
      COMMAND
        $<TARGET_FILE:vice_collision_cooker>
        --input "${_vice_lab_terrain_source}"
        --output "${_vice_lab_terrain_collision}"
      DEPENDS
        vice_collision_cooker
        "${_vice_lab_terrain_source}"
      VERBATIM
    )

    add_custom_target(
      vice_lab_scene_assets
      DEPENDS
        "${_vice_lab_scene_mesh}"
        "${_vice_lab_scene_collision}"
        "${_vice_lab_terrain_collision}"
    )
    add_dependencies(vice_lab vice_lab_scene_assets)
    target_compile_definitions(
      vice_lab
      PRIVATE
        VICE_LAB_COOKED_SCENE_MESH="${_vice_lab_scene_mesh}"
        VICE_LAB_COOKED_SCENE_COLLISION="${_vice_lab_scene_collision}"
        VICE_LAB_COOKED_TERRAIN_COLLISION="${_vice_lab_terrain_collision}"
    )
    message(STATUS "VICE Lab authored scene cooking enabled.")
  endif()
endif()
