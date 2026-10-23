#include <vice/runtime/render_residency.hpp>

#include <algorithm>
#include <cstdint>

namespace vice::runtime {
namespace {

[[nodiscard]] bool valid_fidelity(const world::RegionFidelity fidelity) noexcept {
    switch (fidelity) {
    case world::RegionFidelity::inactive:
    case world::RegionFidelity::reduced:
    case world::RegionFidelity::full:
        return true;
    }
    return false;
}

[[nodiscard]] auto find_region(
    const RuntimeRenderResidencyPlan& plan,
    const world::RegionId region
) noexcept {
    return std::lower_bound(
        plan.regions.begin(),
        plan.regions.end(),
        region,
        [](const RuntimeRenderResidentRegion& entry, const world::RegionId value) {
            return entry.region.value < value.value;
        }
    );
}

} // namespace

bool RuntimeRenderResidencyPlan::resident(const world::RegionId region) const noexcept {
    const auto found = find_region(*this, region);
    return found != regions.end() && found->region == region;
}

std::optional<world::StreamingCellId> RuntimeRenderResidencyPlan::cell(
    const world::RegionId region
) const noexcept {
    const auto found = find_region(*this, region);
    if (found == regions.end() || found->region != region) return std::nullopt;
    return found->cell;
}

std::optional<world::RegionFidelity> RuntimeRenderResidencyPlan::fidelity(
    const world::RegionId region
) const noexcept {
    const auto found = find_region(*this, region);
    if (found == regions.end() || found->region != region) return std::nullopt;
    return found->fidelity;
}

RuntimeRenderResidencyBuildResult build_runtime_render_residency(
    const world::WorldSnapshot& snapshot
) {
    RuntimeRenderResidencyBuildResult result;
    std::vector<world::RegionDesc> regions = snapshot.regions;
    std::sort(regions.begin(), regions.end(), [](const auto& left, const auto& right) {
        return left.id.value < right.id.value;
    });

    world::RegionId previous{};
    bool have_previous = false;
    result.plan.regions.reserve(regions.size());
    for (const auto& region : regions) {
        if (!region.id.valid() || !valid_fidelity(region.fidelity)) {
            result.plan = {};
            result.diagnostics = "Runtime render residency rejected invalid World region metadata.";
            return result;
        }
        if (have_previous && region.id == previous) {
            result.plan = {};
            result.diagnostics = "Runtime render residency rejected duplicate World region IDs.";
            return result;
        }
        previous = region.id;
        have_previous = true;
        if (region.fidelity == world::RegionFidelity::inactive) continue;
        result.plan.regions.push_back(RuntimeRenderResidentRegion{
            .region = region.id,
            .cell = region.streaming_cell,
            .fidelity = region.fidelity,
        });
    }
    return result;
}

} // namespace vice::runtime
