#pragma once

#include <vice/render/rhi.hpp>
#include <vice/runtime/virtual_texture_sampling.hpp>

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace vice::runtime {

struct RuntimeGpuVirtualTextureFeedbackResult {
    std::vector<RuntimeVirtualTextureTileKey> requests;
    std::string diagnostics;

    [[nodiscard]] explicit operator bool() const noexcept {
        return diagnostics.empty();
    }
};

[[nodiscard]] RuntimeGpuVirtualTextureFeedbackResult
build_runtime_gpu_virtual_texture_feedback_requests(
    const RuntimeVirtualTextureSamplingPlan& sampling,
    std::span<const std::uint32_t> page_flags
);

class RuntimeGpuVirtualTextureFeedback final {
public:
    RuntimeGpuVirtualTextureFeedback();
    ~RuntimeGpuVirtualTextureFeedback();

    RuntimeGpuVirtualTextureFeedback(const RuntimeGpuVirtualTextureFeedback&) = delete;
    RuntimeGpuVirtualTextureFeedback& operator=(const RuntimeGpuVirtualTextureFeedback&) = delete;
    RuntimeGpuVirtualTextureFeedback(RuntimeGpuVirtualTextureFeedback&&) noexcept;
    RuntimeGpuVirtualTextureFeedback& operator=(RuntimeGpuVirtualTextureFeedback&&) noexcept;

    [[nodiscard]] bool initialize(
        render::Device& device,
        std::string* diagnostics = nullptr
    );

    [[nodiscard]] RuntimeGpuVirtualTextureFeedbackResult poll();

    [[nodiscard]] bool prepare(
        render::CommandList& commands,
        const RuntimeVirtualTextureSamplingPlan& sampling,
        std::string* diagnostics = nullptr
    );

    [[nodiscard]] render::Buffer* binding_buffer() const noexcept;
    [[nodiscard]] bool active() const noexcept;

    [[nodiscard]] bool capture(
        render::CommandList& commands,
        std::string* diagnostics = nullptr
    );

    [[nodiscard]] bool commit(
        render::SubmissionId submission,
        std::string* diagnostics = nullptr
    );

    void cancel() noexcept;

private:
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace vice::runtime
