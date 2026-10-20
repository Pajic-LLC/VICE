add_library(vice_render STATIC)
add_library(VICE::Render ALIAS vice_render)

target_include_directories(
  vice_render
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_sources(
  vice_render
  PRIVATE
    src/compute.cpp
    src/environment_asset.cpp
    src/material_asset.cpp
    src/mesh_asset.cpp
    src/pipeline_layout.cpp
    src/shader_asset.cpp
    src/telemetry.cpp
    src/texture_asset.cpp
)
target_link_libraries(vice_render PUBLIC VICE::Core VICE::Platform)
target_compile_features(vice_render PUBLIC cxx_std_23)
vice_enable_warnings(vice_render)

if(APPLE)
  enable_language(OBJCXX)
  target_sources(
    vice_render
    PRIVATE
      src/metal/compute_pipeline.mm
      src/metal/rhi.mm
  )
  set_target_properties(
    vice_render
    PROPERTIES
      OBJCXX_STANDARD 23
      OBJCXX_STANDARD_REQUIRED YES
      OBJCXX_EXTENSIONS NO
  )
  set_source_files_properties(
    src/metal/compute_pipeline.mm
    src/metal/rhi.mm
    PROPERTIES COMPILE_OPTIONS "-fobjc-arc"
  )

  find_library(APPKIT_FRAMEWORK_RENDER AppKit REQUIRED)
  find_library(METAL_FRAMEWORK Metal REQUIRED)
  find_library(QUARTZCORE_FRAMEWORK QuartzCore REQUIRED)
  target_link_libraries(
    vice_render
    PRIVATE
      ${APPKIT_FRAMEWORK_RENDER}
      ${METAL_FRAMEWORK}
      ${QUARTZCORE_FRAMEWORK}
  )
elseif(WIN32)
  target_sources(
    vice_render
    PRIVATE
      src/d3d12/compute_pipeline.cpp
      src/d3d12/rhi.cpp
      src/d3d12/root_signature.cpp
  )
  target_compile_definitions(vice_render PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
  target_link_libraries(vice_render PRIVATE d3d12 d3dcompiler dxgi dxguid)
else()
  message(FATAL_ERROR "VICE rendering currently supports Metal on macOS and Direct3D 12 on Windows.")
endif()
