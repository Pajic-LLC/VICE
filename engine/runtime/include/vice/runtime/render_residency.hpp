#pragma once

#include <vice/world/snapshot.hpp>
#include <vice/world/spatial.hpp>

#include <optional>
#include <string>
#include <vector>

namespace vice::runtime {

struct RuntimeRenderResidentRegion {
    world::RegionId region{};
    world::StreamingCellId cell{};
    world::RegionFidelity fidelity = world::RegionFidelity::inactive;

    friend bool operator==(const RuntimeRenderResidentRegion&, const RuntimeRenderResidentRegion&) = default;
};

struct RuntimeRenderResidencyPlan {
    std::vector<RuntimeRenderResidentRegion> regions;

    [[nodiscard]] bool resident(world::RegionId region) const noexcept;
    [[nodiscard]] std::optional<world::StreamingCellId> cell(world::RegionId region) const noexcept;
    [[nodiscard]] std::optional<world::RegionFidelity> fidelity(world::RegionId region) const noexcept;
};

struct RuntimeRenderResidencyBuildResult {
    RuntimeRenderResidencyPlan plan;
    std::string diagnostics;

    [[nodiscard]] explicit operator bool() const noexcept {
        return diagnostics.empty();
    }
};

[[nodiscard]] RuntimeRenderResidencyBuildResult build_runtime_render_residency(
    const world::WorldSnapshot& snapshot
);

} // namespace vice::runtime
