#pragma once

#include <vice/runtime/scene.hpp>

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace vice::runtime {

inline constexpr std::uint32_t runtime_ray_tracing_invalid_scene_instance = 0xFFFFFFFFU;

struct RuntimeRayTracingBlasKey {
    world::ResourceId mesh{};
    render::MeshStableId stable_id{};
    std::uint64_t source_fingerprint = 0U;
    std::uint32_t vertex_count = 0U;
    std::uint32_t index_count = 0U;

    friend bool operator==(const RuntimeRayTracingBlasKey&, const RuntimeRayTracingBlasKey&) = default;
};

struct RuntimeRayTracingBlasPlan {
    RuntimeRayTracingBlasKey key{};

    friend bool operator==(const RuntimeRayTracingBlasPlan&, const RuntimeRayTracingBlasPlan&) = default;
};

struct RuntimeRayTracingInstancePlan {
    world::PersistentId entity{};
    std::uint32_t instance_id = 0U;
    std::uint32_t blas_index = 0U;
    std::uint32_t mask = 0xFFU;
    std::array<float, 12> transform{
        1.0F, 0.0F, 0.0F, 0.0F,
        0.0F, 1.0F, 0.0F, 0.0F,
        0.0F, 0.0F, 1.0F, 0.0F,
    };

    friend bool operator==(const RuntimeRayTracingInstancePlan&, const RuntimeRayTracingInstancePlan&) = default;
};

struct RuntimeRayTracingScenePlan {
    std::vector<RuntimeRayTracingBlasPlan> blases;
    std::vector<RuntimeRayTracingInstancePlan> instances;
    // Indexed by TLAS array instance index. Values address the authoritative
    // RenderMeshInstance span supplied to the planner; they are deliberately
    // independent of the persistent 24-bit custom InstanceID identity.
    std::vector<std::uint32_t> scene_instance_indices;
    std::string diagnostics;

    [[nodiscard]] explicit operator bool() const noexcept {
        return diagnostics.empty();
    }

    [[nodiscard]] std::uint32_t scene_instance_index(
        const std::uint32_t tlas_instance_index
    ) const noexcept {
        return tlas_instance_index < scene_instance_indices.size()
            ? scene_instance_indices[tlas_instance_index]
            : runtime_ray_tracing_invalid_scene_instance;
    }
};

[[nodiscard]] RuntimeRayTracingScenePlan build_runtime_ray_tracing_scene_plan(
    std::span<const RenderMeshInstance> instances,
    std::span<const CookedMeshRecord> meshes
);

// Builds the same authoritative identity/BLAS plan, but expresses every TLAS instance
// transform in the supplied global origin's local float frame. This keeps backend AS
// transforms precise without exposing backend-native coordinates to game-facing APIs.
[[nodiscard]] RuntimeRayTracingScenePlan build_runtime_ray_tracing_scene_plan(
   std::span<const RenderMeshInstance> instances,
   std::span<const CookedMeshRecord> meshes,
   world::GlobalPosition origin
);

} // namespace vice::runtime
