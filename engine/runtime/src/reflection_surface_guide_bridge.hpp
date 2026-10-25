#pragma once

#include <vice/runtime/gpu_cluster_instance_remap.hpp>
#include <vice/runtime/gpu_draw_batches.hpp>
#include <vice/runtime/gpu_instance_table.hpp>
#include <vice/runtime/reflection_surface_guide.hpp>
#include <vice/runtime/scene.hpp>

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

[[nodiscard]] render::Texture* runtime_reflection_surface_guide_texture(
    const RuntimeSceneRenderer* owner
) noexcept;

} // namespace vice::runtime::detail

#if defined(render)

#ifdef runtime_directional_shadow_requires_depth_prepass
#undef runtime_directional_shadow_requires_depth_prepass
#endif
// Keep this unparenthesized. The legacy caller uses
// `shadow_view.has_value() && runtime_directional_shadow_requires_depth_prepass(path)`;
// `&&` must bind only to the directional-shadow term so AO and the reflection guide
// can independently request authoritative current depth.
#define runtime_directional_shadow_requires_depth_prepass(path) \
    ::vice::runtime::runtime_directional_shadow_requires_depth_prepass((path)) || \
        ::vice::runtime::detail::runtime_ray_ambient_occlusion_needs_camera_depth_prepass() || \
        ::vice::runtime::detail::runtime_reflection_surface_guide_needs_camera_depth_prepass( \
            state_->device \
        )

#ifdef encode_motion_vectors
#undef encode_motion_vectors
#endif

#define encode_motion_vectors(...) \
    (encode_motion_vectors(__VA_ARGS__) && \
        ::vice::runtime::detail::runtime_ray_ambient_occlusion_after_motion_vectors( \
            *commands, state_->motion_vectors.get(), current_depth, previous_depth, temporal_frame \
        ) && \
        ::vice::runtime::detail::runtime_reflection_surface_guide_after_motion_vectors( \
            this, \
            *state_->device, \
            *commands, \
            current_depth, \
            state_->gpu_pbr_frame.get(), \
            sizeof(GpuPbrSceneConstants), \
            state_->gpu_instances.result(), \
            gpu_pbr_batch_result, \
            state_->gpu_cluster_pbr_instances.result(), \
            gpu_cluster_pbr_batch_result, \
            state_->gpu_material_scene.view(), \
            std::span<const world::ResourceId>{gpu_pbr_plan.batch_meshes}, \
            std::span<const world::ResourceId>{gpu_cluster_pbr_plan.batch_meshes}, \
            [&](const world::ResourceId resource_id) \
                -> ::vice::runtime::detail::RuntimeReflectionSurfaceGuideMeshBuffers { \
                const auto found = state_->meshes.find(resource_id); \
                if (found == state_->meshes.end()) return {}; \
                return ::vice::runtime::detail::RuntimeReflectionSurfaceGuideMeshBuffers{ \
                    .vertices = found->second.vertices.get(), \
                    .indices = found->second.indices.get(), \
                }; \
            }, \
            diagnostics \
        ))

#endif
