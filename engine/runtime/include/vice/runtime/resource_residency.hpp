#pragma once

#include <vice/runtime/render_streaming.hpp>
#include <vice/runtime/texture.hpp>

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace vice::runtime {

struct RuntimeResourceResidencyCandidate {
    world::ResourceId resource{};
    std::uint64_t bytes = 0U;
    world::RegionFidelity fidelity = world::RegionFidelity::inactive;
};

struct RuntimeResourceResidencyPlan {
    std::vector<world::ResourceId> resources;
    std::uint64_t resident_bytes = 0U;
    std::uint64_t budget_bytes = 0U;

    [[nodiscard]] bool resident(world::ResourceId resource) const noexcept;
};

struct RuntimeResourceResidencyBuildResult {
    RuntimeResourceResidencyPlan plan;
    std::string diagnostics;

    [[nodiscard]] explicit operator bool() const noexcept {
        return diagnostics.empty();
    }
};

[[nodiscard]] RuntimeResourceResidencyBuildResult build_runtime_resource_residency_plan(
    std::span<const RuntimeResourceResidencyCandidate> candidates,
    std::uint64_t budget_bytes
);

struct RuntimeRenderResourceResidency {
    RuntimeResourceResidencyPlan geometry;
    RuntimeResourceResidencyPlan textures;
};

struct RuntimeRenderResourceResidencyBuildResult {
    RuntimeRenderResourceResidency residency;
    std::string diagnostics;

    [[nodiscard]] explicit operator bool() const noexcept {
        return diagnostics.empty();
    }
};

[[nodiscard]] RuntimeRenderResourceResidencyBuildResult build_runtime_render_resource_residency(
    const RuntimeRenderResidentScene& scene,
    const CookedMeshCatalog& meshes,
    const CookedRenderMaterialCatalog& materials,
    const CookedTextureCatalog& textures,
    RuntimeRenderResourceBudgets budgets
);

[[nodiscard]] std::vector<RenderMeshInstance> filter_runtime_resident_geometry(
    std::span<const RenderMeshInstance> instances,
    const RuntimeResourceResidencyPlan& geometry
);

} // namespace vice::runtime
