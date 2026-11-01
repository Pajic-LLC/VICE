#pragma once

#include <vice/render/rhi.hpp>
#include <vice/runtime/visibility.hpp>

#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <string>

namespace vice::runtime {

inline constexpr std::uint32_t runtime_gpu_draw_batch_unassigned =
    std::numeric_limits<std::uint32_t>::max();

struct RuntimeGpuDrawBatch {
    std::uint32_t index_count = 0U;
    std::uint32_t first_index = 0U;
    std::int32_t vertex_offset = 0;
};

[[nodiscard]] constexpr bool valid(const RuntimeGpuDrawBatch& batch) noexcept {
    return batch.index_count > 0U;
}

struct RuntimeGpuDrawBatchRange {
    std::uint32_t offset = 0U;
    std::uint32_t capacity = 0U;
};

struct RuntimeGpuDrawBatchResult {
    render::Buffer* compacted_instance_indices = nullptr;
    render::Buffer* indirect_arguments = nullptr;
    // Offsets/capacities count uint32 indices. Indirect first_instance is zero;
    // bind this subview before each draw and skip zero-capacity ranges.
    // The span belongs to the batcher until its next build/initialize or destruction.
    std::span<const RuntimeGpuDrawBatchRange> ranges;
    std::uint32_t batch_count = 0U;
    std::uint32_t instance_capacity = 0U;
};

class RuntimeGpuDrawBatcher final {
public:
    RuntimeGpuDrawBatcher();
    ~RuntimeGpuDrawBatcher();

    RuntimeGpuDrawBatcher(const RuntimeGpuDrawBatcher&) = delete;
    RuntimeGpuDrawBatcher& operator=(const RuntimeGpuDrawBatcher&) = delete;
    RuntimeGpuDrawBatcher(RuntimeGpuDrawBatcher&&) noexcept;
    RuntimeGpuDrawBatcher& operator=(RuntimeGpuDrawBatcher&&) noexcept;

    [[nodiscard]] bool initialize(
        render::Device& device,
        std::string* diagnostics = nullptr
    );

    [[nodiscard]] bool build(
        render::CommandList& commands,
        const RuntimeVisibilityGpuResult& visibility,
        std::span<const std::uint32_t> instance_batches,
        std::span<const RuntimeGpuDrawBatch> batches,
        std::string* diagnostics = nullptr
    );

    [[nodiscard]] RuntimeGpuDrawBatchResult result() const noexcept;

private:
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace vice::runtime
