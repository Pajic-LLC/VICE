#pragma once

#include <vice/runtime/gpu_draw_batches.hpp>
#include <vice/runtime/scene.hpp>
#include <vice/runtime/visibility.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace vice::runtime {

inline constexpr float runtime_virtual_geometry_default_max_error_pixels = 2.0F;

struct RuntimeGeometryVariantKey {
    world::ResourceId mesh{};
    std::uint32_t node_index = render::virtual_geometry_invalid_node;

    friend bool operator==(const RuntimeGeometryVariantKey&, const RuntimeGeometryVariantKey&) = default;
};

struct RuntimeGeometryVariantKeyHash {
    [[nodiscard]] std::size_t operator()(const RuntimeGeometryVariantKey& key) const noexcept;
};

struct RuntimeVirtualGeometryBufferPlan {
    std::vector<render::MeshVertex> vertices;
    std::vector<std::uint32_t> indices;
};

struct RuntimeVirtualGeometryBufferBuildResult {
    std::optional<RuntimeVirtualGeometryBufferPlan> plan;
    std::string diagnostics;

    [[nodiscard]] explicit operator bool() const noexcept {
        return plan.has_value();
    }
};

struct RuntimeVirtualGeometryVariantBuildResult {
    std::optional<render::MeshAsset> asset;
    std::string diagnostics;

    [[nodiscard]] explicit operator bool() const noexcept {
        return asset.has_value();
    }
};

struct RuntimeVirtualGeometryInstanceBuildResult {
    std::vector<RenderMeshInstance> instances;
    std::string diagnostics;

    [[nodiscard]] explicit operator bool() const noexcept {
        return diagnostics.empty();
    }
};

[[nodiscard]] RuntimeGeometryVariantKey runtime_geometry_variant_key(
    const RenderMeshInstance& instance
) noexcept;

[[nodiscard]] world::ResourceId runtime_virtual_geometry_variant_resource_id(
    world::ResourceId mesh,
    std::uint32_t node_index
) noexcept;

[[nodiscard]] RuntimeVirtualGeometryBufferBuildResult build_runtime_virtual_geometry_buffer_plan(
    const render::MeshAsset& asset
);

[[nodiscard]] RuntimeVirtualGeometryVariantBuildResult build_runtime_virtual_geometry_variant_asset(
    const render::MeshAsset& asset,
    std::uint32_t node_index
);

[[nodiscard]] std::optional<RuntimeGpuDrawBatch> runtime_virtual_geometry_draw_batch(
    const render::MeshAsset& asset,
    std::uint32_t node_index,
    std::string* diagnostics = nullptr
);

[[nodiscard]] RuntimeVirtualGeometryInstanceBuildResult select_runtime_virtual_geometry_instances(
    std::span<const RenderMeshInstance> instances,
    std::span<const CookedMeshRecord> meshes,
    const RuntimeVisibilityFrustum& frustum,
    world::GlobalPosition camera_global_position,
    float vertical_fov_radians,
    float viewport_height_pixels,
    float max_projected_error_pixels = runtime_virtual_geometry_default_max_error_pixels
);

} // namespace vice::runtime
