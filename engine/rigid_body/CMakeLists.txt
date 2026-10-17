include(FetchContent)

set(USE_ASSERTS OFF CACHE BOOL "" FORCE)
set(DOUBLE_PRECISION OFF CACHE BOOL "" FORCE)
set(GENERATE_DEBUG_SYMBOLS OFF CACHE BOOL "" FORCE)
set(OVERRIDE_CXX_FLAGS OFF CACHE BOOL "" FORCE)
set(CROSS_PLATFORM_DETERMINISTIC ON CACHE BOOL "" FORCE)
set(INTERPROCEDURAL_OPTIMIZATION OFF CACHE BOOL "" FORCE)
set(FLOATING_POINT_EXCEPTIONS_ENABLED OFF CACHE BOOL "" FORCE)
set(CPP_EXCEPTIONS_ENABLED OFF CACHE BOOL "" FORCE)
set(CPP_RTTI_ENABLED OFF CACHE BOOL "" FORCE)
set(ENABLE_ALL_WARNINGS OFF CACHE BOOL "" FORCE)
set(DEBUG_RENDERER_IN_DEBUG_AND_RELEASE OFF CACHE BOOL "" FORCE)
set(DEBUG_RENDERER_IN_DISTRIBUTION OFF CACHE BOOL "" FORCE)
set(PROFILER_IN_DEBUG_AND_RELEASE OFF CACHE BOOL "" FORCE)
set(PROFILER_IN_DISTRIBUTION OFF CACHE BOOL "" FORCE)
set(ENABLE_OBJECT_STREAM OFF CACHE BOOL "" FORCE)
set(ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(JPH_USE_DX12 OFF CACHE BOOL "" FORCE)
set(JPH_USE_VK OFF CACHE BOOL "" FORCE)
set(JPH_USE_MTL OFF CACHE BOOL "" FORCE)
set(JPH_USE_CPU_COMPUTE OFF CACHE BOOL "" FORCE)
set(USE_STATIC_MSVC_RUNTIME_LIBRARY OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
  JoltPhysics
  GIT_REPOSITORY "https://github.com/jrouwe/JoltPhysics"
  GIT_TAG "v5.6.0"
  GIT_SHALLOW TRUE
  SOURCE_SUBDIR "Build"
)
FetchContent_MakeAvailable(JoltPhysics)

add_library(
  vice_rigid_body
  STATIC
    src/collision_asset.cpp
    src/collision_asset_file.cpp
    src/debug.cpp
    src/material_asset.cpp
    src/jolt_job_system.cpp
    src/jolt_rigid_body_jobs.cpp
    src/scheduler_domain.cpp
)

add_library(VICE::RigidBody ALIAS vice_rigid_body)

target_include_directories(
  vice_rigid_body
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  PRIVATE
    "${JoltPhysics_SOURCE_DIR}/.."
)

target_link_libraries(
  vice_rigid_body
  PUBLIC
    VICE::Jobs
    VICE::Math
    VICE::Simulation
  PRIVATE
    Jolt
)
target_compile_features(vice_rigid_body PUBLIC cxx_std_23)
if(MSVC)
  target_compile_options(vice_rigid_body PRIVATE /GR-)
else()
  target_compile_options(vice_rigid_body PRIVATE -fno-rtti)
endif()
vice_enable_warnings(vice_rigid_body)

add_library(
  vice_rigid_body_world
  STATIC
    src/world_activation.cpp
    src/world_box_orchestration.cpp
    src/world_bridge.cpp
    src/world_orchestration.cpp
)

add_library(VICE::RigidBodyWorld ALIAS vice_rigid_body_world)

target_include_directories(
  vice_rigid_body_world
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(
  vice_rigid_body_world
  PUBLIC
    VICE::RigidBody
    VICE::SimulationWorld
    VICE::World
)
target_compile_features(vice_rigid_body_world PUBLIC cxx_std_23)
vice_enable_warnings(vice_rigid_body_world)
