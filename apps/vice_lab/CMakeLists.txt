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
endif()
