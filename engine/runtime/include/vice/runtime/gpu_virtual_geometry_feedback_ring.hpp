#pragma once

#include <vice/runtime/gpu_virtual_geometry_feedback.hpp>
#include <vice/runtime/scene.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>

namespace vice::runtime {

inline constexpr std::uint32_t runtime_gpu_virtual_geometry_feedback_lane_count = 2U;

struct RuntimeGpuVirtualGeometryFeedbackTicket {
    std::uint32_t slot = 0xFFFFFFFFU;
    std::uint64_t serial = 0U;

    [[nodiscard]] bool valid() const noexcept {
        return slot != 0xFFFFFFFFU;
    }
};

struct RuntimeGpuVirtualGeometryFeedbackAcquireResult {
    std::optional<RuntimeGpuVirtualGeometryFeedbackTicket> ticket;
    std::string diagnostics;

    [[nodiscard]] explicit operator bool() const noexcept {
        return diagnostics.empty();
    }
};

struct RuntimeGpuVirtualGeometryFeedbackPollResult {
    std::optional<RuntimeGpuVirtualGeometryFeedbackResult> feedback;
    std::uint64_t serial = 0U;
    std::string diagnostics;

    [[nodiscard]] explicit operator bool() const noexcept {
        return diagnostics.empty();
    }
};

[[nodiscard]] RuntimeGpuVirtualGeometryFeedbackResult
build_runtime_gpu_virtual_geometry_root_feedback(
    const RuntimeGpuVirtualGeometryPlan& plan
);

class RuntimeGpuVirtualGeometryFeedbackRing final {
public:
    RuntimeGpuVirtualGeometryFeedbackRing();
    ~RuntimeGpuVirtualGeometryFeedbackRing();

    RuntimeGpuVirtualGeometryFeedbackRing(
        const RuntimeGpuVirtualGeometryFeedbackRing&
    ) = delete;
    RuntimeGpuVirtualGeometryFeedbackRing& operator=(
        const RuntimeGpuVirtualGeometryFeedbackRing&
    ) = delete;
    RuntimeGpuVirtualGeometryFeedbackRing(
        RuntimeGpuVirtualGeometryFeedbackRing&&
    ) noexcept;
    RuntimeGpuVirtualGeometryFeedbackRing& operator=(
        RuntimeGpuVirtualGeometryFeedbackRing&&
    ) noexcept;

    [[nodiscard]] bool initialize(
        render::Device& device,
        std::string* diagnostics = nullptr
    );

    [[nodiscard]] RuntimeGpuVirtualGeometryFeedbackPollResult poll(
        const RuntimeGpuVirtualGeometryPlan& current_plan,
        std::span<const RenderMeshInstance> current_instances
    );

    [[nodiscard]] RuntimeGpuVirtualGeometryFeedbackAcquireResult acquire(
        const RuntimeGpuVirtualGeometryPlan& plan,
        std::span<const RenderMeshInstance> instances
    );

    [[nodiscard]] bool capture(
        render::CommandList& commands,
        const RuntimeGpuVirtualGeometrySelectionResult& selection,
        RuntimeGpuVirtualGeometryFeedbackTicket ticket,
        std::string* diagnostics = nullptr
    );

    [[nodiscard]] bool commit(
        RuntimeGpuVirtualGeometryFeedbackTicket ticket,
        render::SubmissionId submission,
        std::string* diagnostics = nullptr
    );

    void cancel(RuntimeGpuVirtualGeometryFeedbackTicket ticket) noexcept;

    [[nodiscard]] std::uint32_t in_flight_count() const noexcept;

private:
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace vice::runtime
