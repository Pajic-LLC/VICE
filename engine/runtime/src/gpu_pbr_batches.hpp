#pragma once

#include <vice/runtime/gpu_draw_batches.hpp>
#include <vice/runtime/scene.hpp>

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace vice::runtime::detail {

struct RuntimeGpuPbrBatchPlan {
    std::vector<std::uint32_t> instance_batches;
    std::vector<RuntimeGpuDrawBatch> batches;
    std::vector<world::ResourceId> batch_meshes;
};

struct RuntimeGpuPbrBatchBuildResult {
    RuntimeGpuPbrBatchPlan plan;
    std::string diagnostics;

    [[nodiscard]] explicit operator bool() const noexcept {
        return diagnostics.empty();
    }
};

[[nodiscard]] RuntimeGpuPbrBatchBuildResult build_runtime_gpu_pbr_batch_plan(
    std::span<const RenderMeshInstance> instances,
    std::span<const CookedMeshRecord> meshes,
    std::span<const CookedRenderMaterialRecord> materials
);

} // namespace vice::runtime::detail
