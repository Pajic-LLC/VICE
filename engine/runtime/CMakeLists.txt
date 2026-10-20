add_library(
  vice_runtime
  STATIC
    src/environment_pipeline.cpp
    src/gpu_draw_batches.cpp
    src/gpu_instance_materials.cpp
    src/gpu_instance_table.cpp
    src/gpu_material_resources.cpp
    src/gpu_material_table.cpp
    src/material_pipeline.cpp
    src/module_loader.cpp
    src/module_paths.cpp
    src/post_process.cpp
    src/runtime.cpp
    src/scene.cpp
    src/texture_pipeline.cpp
    src/visibility.cpp
)

add_library(VICE::Runtime ALIAS vice_runtime)

target_include_directories(
  vice_runtime
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(
  vice_runtime
  PUBLIC
    VICE::Core
    VICE::Math
    VICE::Platform
    VICE::Project
    VICE::Render
    VICE::World
)

target_compile_features(vice_runtime PUBLIC cxx_std_23)
vice_enable_warnings(vice_runtime)
