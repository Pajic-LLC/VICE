#pragma once

#include <vice/runtime/gpu_virtual_geometry.hpp>

#include <memory>
#include <span>
#include <string>
#include <vector>

namespace vice::runtime {

struct RuntimeGpuVirtualGeometryFeedbackResult {
    std::vector<RuntimeGpuVirtualGeometryNodeRequest> node_requests;
    std::vector<RuntimeGpuVirtualGeometryPageRequest> page_requests;
    std::string diagnostics;

    [[nodiscard]] explicit operator bool() const noexcept {
        return diagnostics.empty();
    }
};

[[nodiscard]] RuntimeGpuVirtualGeometryFeedbackResult
validate_runtime_gpu_virtual_geometry_feedback(
    std::span<const RuntimeGpuVirtualGeometryNodeRequest> node_requests,
    std::span<const RuntimeGpuVirtualGeometryPageRequest> page_requests,
    const RuntimeGpuVirtualGeometryPlan& plan
);

class RuntimeGpuVirtualGeometryFeedback final {
public:
    RuntimeGpuVirtualGeometryFeedback();
    ~RuntimeGpuVirtualGeometryFeedback();

    RuntimeGpuVirtualGeometryFeedback(const RuntimeGpuVirtualGeometryFeedback&) = delete;
    RuntimeGpuVirtualGeometryFeedback& operator=(const RuntimeGpuVirtualGeometryFeedback&) = delete;
    RuntimeGpuVirtualGeometryFeedback(RuntimeGpuVirtualGeometryFeedback&&) noexcept;
    RuntimeGpuVirtualGeometryFeedback& operator=(RuntimeGpuVirtualGeometryFeedback&&) noexcept;

    [[nodiscard]] bool initialize(
        render::Device& device,
        std::string* diagnostics = nullptr
    );

    [[nodiscard]] bool capture(
        render::CommandList& commands,
        const RuntimeGpuVirtualGeometrySelectionResult& selection,
        std::string* diagnostics = nullptr
    );

    [[nodiscard]] RuntimeGpuVirtualGeometryFeedbackResult read(
        const RuntimeGpuVirtualGeometryPlan& plan
    ) const;

private:
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace vice::runtime
