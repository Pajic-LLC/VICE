#pragma once

#include <vice/runtime/gpu_cluster_instance_remap.hpp>
#include <vice/runtime/gpu_draw_batches.hpp>
#include <vice/runtime/gpu_instance_table.hpp>
#include <vice/runtime/post_process.hpp>
#include <vice/runtime/ray_reflection_history.hpp>
#include <vice/runtime/ray_reflections.hpp>
#include <vice/runtime/ray_tracing_scene_executor.hpp>
#include <vice/runtime/reflection_surface_guide.hpp>
#include <vice/runtime/scene.hpp>

#include "environment_pipeline.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>

namespace vice::runtime::detail {

struct RuntimeGpuMaterialSceneView;

struct RuntimeReflectionSurfaceGuideMeshBuffers {
    render::Buffer* vertices = nullptr;
    render::Buffer* indices = nullptr;
};

[[nodiscard]] inline bool runtime_reflection_surface_guide_needs_camera_depth_prepass(
    const render::Device* const device
) noexcept {
#if defined(VICE_RUNTIME_REFLECTION_SURFACE_GUIDE_VERTEX_MANIFEST) && \
    defined(VICE_RUNTIME_REFLECTION_SURFACE_GUIDE_FRAGMENT_MANIFEST)
    return device != nullptr && device->capabilities().ray_tracing;
#else
    (void)device;
    return false;
#endif
}

[[nodiscard]] bool runtime_reflection_surface_guide_after_motion_vectors(
    const RuntimeSceneRenderer* owner,
    render::Device& device,
    render::CommandList& commands,
    render::Texture* current_depth,
    render::Buffer* frame_constants,
    std::uint64_t frame_constants_size,
    RuntimeGpuInstanceTableResult instance_table,
    const RuntimeGpuDrawBatchResult& direct_batches,
    RuntimeGpuClusterInstanceRemapResult cluster_instances,
    const RuntimeGpuDrawBatchResult& cluster_batches,
    RuntimeGpuMaterialSceneView material_scene,
    std::span<const world::ResourceId> direct_meshes,
    std::span<const world::ResourceId> cluster_meshes,
    const std::function<RuntimeReflectionSurfaceGuideMeshBuffers(world::ResourceId)>& resolve_mesh,
    std::string* diagnostics = nullptr
) noexcept;

[[nodiscard]] bool runtime_ray_reflections_after_surface_guide(
    const RuntimeSceneRenderer* owner,
    render::Device& device,
    render::CommandList& commands,
    render::Texture* current_depth,
    render::Texture* surface_guide,
    RuntimeRayTracingSceneExecutor* ray_scene,
    const RuntimeCameraView& camera,
    float jitter_x_ndc,
    float jitter_y_ndc,
    platform::WindowMetrics metrics,
    RuntimeEnvironmentBinding environment,
    std::string* diagnostics = nullptr
) noexcept;

[[nodiscard]] bool runtime_ray_reflection_history_after_trace(
    const RuntimeSceneRenderer* owner,
    render::Device& device,
    render::CommandList& commands,
    render::Texture* reflection_radiance,
    render::Buffer* reflection_hits,
    render::Texture* previous_scene_color,
    render::Texture* previous_depth,
    const ::vice::runtime::RuntimeTemporalFrameState& temporal,
    platform::WindowMetrics metrics,
    std::string* diagnostics = nullptr
) noexcept;

[[nodiscard]] render::Texture* runtime_reflection_surface_guide_texture(
    const RuntimeSceneRenderer* owner
) noexcept;

[[nodiscard]] render::Texture* runtime_ray_reflection_radiance(
    const RuntimeSceneRenderer* owner
) noexcept;

[[nodiscard]] render::Buffer* runtime_ray_reflection_hits(
    const RuntimeSceneRenderer* owner
) noexcept;

[[nodiscard]] inline bool runtime_ray_reflections_mark_gpu_pbr(
    const RuntimeSceneRenderer* const owner,
    render::CommandList& commands,
    render::Buffer* frame_constants,
    const std::uint64_t frame_constants_size,
    std::string* const diagnostics
) noexcept {
    (void)owner;
    (void)commands;
    (void)frame_constants;
    (void)frame_constants_size;
    if (diagnostics != nullptr) diagnostics->clear();
    return true;
}

} // namespace vice::runtime::detail
