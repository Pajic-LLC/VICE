#pragma once

#include <vice/render/rhi.hpp>
#include <vice/runtime/gpu_cluster_visibility.hpp>
#include <vice/runtime/gpu_draw_batches.hpp>

#include <cstdint>
#include <memory>
#include <string>

namespace vice::runtime {

struct RuntimeGpuClusterInstanceRemapResult {
    render::Buffer* scene_instance_indices = nullptr;
    std::uint32_t capacity = 0U;
};

class RuntimeGpuClusterInstanceRemap final {
public:
    RuntimeGpuClusterInstanceRemap();
    ~RuntimeGpuClusterInstanceRemap();

    RuntimeGpuClusterInstanceRemap(const RuntimeGpuClusterInstanceRemap&) = delete;
    RuntimeGpuClusterInstanceRemap& operator=(const RuntimeGpuClusterInstanceRemap&) = delete;
    RuntimeGpuClusterInstanceRemap(RuntimeGpuClusterInstanceRemap&&) noexcept;
    RuntimeGpuClusterInstanceRemap& operator=(RuntimeGpuClusterInstanceRemap&&) noexcept;

    [[nodiscard]] bool initialize(
        render::Device& device,
        std::string* diagnostics = nullptr
    );

    [[nodiscard]] bool remap(
        render::CommandList& commands,
        const RuntimeGpuDrawBatchResult& batches,
        const RuntimeGpuClusterVisibilityResult& clusters,
        std::string* diagnostics = nullptr
    );

    [[nodiscard]] RuntimeGpuClusterInstanceRemapResult result() const noexcept;

private:
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace vice::runtime
