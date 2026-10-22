#pragma once

#include <vice/runtime/gpu_draw_batches.hpp>
#include <vice/runtime/gpu_instance_table.hpp>
#include <vice/runtime/gpu_meshlet_table.hpp>
#include <vice/runtime/visibility.hpp>

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace vice::runtime {

struct RuntimeGpuClusterCandidate {
    std::uint32_t scene_instance_index = 0U;
    std::uint32_t mesh_index = 0U;
    std::uint32_t meshlet_index = 0U;
    std::uint32_t reserved = 0U;
};

static_assert(sizeof(RuntimeGpuClusterCandidate) == 16U);

struct RuntimeGpuClusterCandidateRange {
    std::uint32_t first_candidate = 0U;
    std::uint32_t candidate_count = 0U;
};

static_assert(sizeof(RuntimeGpuClusterCandidateRange) == 8U);

struct RuntimeGpuClusterCandidatePlan {
    std::vector<RuntimeGpuClusterCandidate> candidates;
    std::vector<RuntimeGpuClusterCandidateRange> instance_ranges;
};

struct RuntimeGpuClusterCandidateBuildResult {
    RuntimeGpuClusterCandidatePlan plan;
    std::string diagnostics;

    [[nodiscard]] explicit operator bool() const noexcept {
        return diagnostics.empty();
    }
};

[[nodiscard]] RuntimeGpuClusterCandidateBuildResult build_runtime_gpu_cluster_candidate_plan(
    std::span<const RenderMeshInstance> instances,
    const RuntimeGpuMeshletTablePlan& meshlets
);

struct RuntimeGpuClusterBatchPlan {
    std::vector<std::uint32_t> candidate_batches;
    std::vector<RuntimeGpuDrawBatch> batches;
    std::vector<world::ResourceId> batch_meshes;
    std::vector<std::uint32_t> batch_meshlets;
};

struct RuntimeGpuClusterBatchBuildResult {
    RuntimeGpuClusterBatchPlan plan;
    std::string diagnostics;

    [[nodiscard]] explicit operator bool() const noexcept {
        return diagnostics.empty();
    }
};

[[nodiscard]] RuntimeGpuClusterBatchBuildResult build_runtime_gpu_cluster_batch_plan(
    const RuntimeGpuClusterCandidatePlan& candidates,
    const RuntimeGpuMeshletTablePlan& meshlets,
    std::span<const std::uint8_t> eligible_scene_instances
);

struct RuntimeGpuClusterVisibilityResult {
    RuntimeVisibilityGpuResult visibility{};
    render::Buffer* candidates = nullptr;
    std::uint32_t candidate_count = 0U;
    std::uint32_t instance_count = 0U;
};

class RuntimeGpuClusterVisibility final {
public:
    RuntimeGpuClusterVisibility();
    ~RuntimeGpuClusterVisibility();

    RuntimeGpuClusterVisibility(const RuntimeGpuClusterVisibility&) = delete;
    RuntimeGpuClusterVisibility& operator=(const RuntimeGpuClusterVisibility&) = delete;
    RuntimeGpuClusterVisibility(RuntimeGpuClusterVisibility&&) noexcept;
    RuntimeGpuClusterVisibility& operator=(RuntimeGpuClusterVisibility&&) noexcept;

    [[nodiscard]] bool initialize(
        render::Device& device,
        std::string* diagnostics = nullptr
    );

    [[nodiscard]] bool cull(
        render::CommandList& commands,
        std::span<const RuntimeGpuClusterCandidate> candidates,
        const RuntimeVisibilityGpuResult& parent_visibility,
        const RuntimeGpuMeshletTableResult& meshlet_table,
        const RuntimeGpuInstanceTableResult& camera_relative_instances,
        const RuntimeVisibilityFrustum& frustum,
        std::string* diagnostics = nullptr
    );

    [[nodiscard]] RuntimeGpuClusterVisibilityResult result() const noexcept;

private:
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace vice::runtime
