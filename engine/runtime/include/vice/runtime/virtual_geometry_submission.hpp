#pragma once

#include <vice/runtime/gpu_virtual_geometry_feedback.hpp>
#include <vice/runtime/virtual_geometry_streaming.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace vice::runtime {

struct RuntimeVirtualGeometrySubmissionGeometry {
    world::ResourceId resource_id{};
    world::ResourceId source_mesh{};
    std::uint32_t node_index = render::virtual_geometry_invalid_node;
    std::optional<std::uint32_t> page_index;
    render::MeshAsset asset;
};

struct RuntimeVirtualGeometrySubmissionResult {
    std::vector<RenderMeshInstance> instances;
    std::vector<RuntimeVirtualGeometrySubmissionGeometry> geometry;
    RuntimeVirtualGeometryResidentPoolUpdateResult residency;
    std::string diagnostics;

    [[nodiscard]] explicit operator bool() const noexcept {
        return diagnostics.empty();
    }
};

[[nodiscard]] RuntimeVirtualGeometrySubmissionResult build_runtime_virtual_geometry_submission(
    std::span<const RenderMeshInstance> source_instances,
    std::span<const CookedMeshRecord> meshes,
    const RuntimeGpuVirtualGeometryPlan& plan,
    const RuntimeGpuVirtualGeometryFeedbackResult& feedback,
    RuntimeVirtualGeometryResidentPool& resident_pool
);

} // namespace vice::runtime
