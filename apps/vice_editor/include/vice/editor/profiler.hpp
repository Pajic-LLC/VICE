#pragma once

#include <vice/runtime/diagnostics.hpp>

#include <cstddef>
#include <span>
#include <vector>

namespace vice::editor {

using WorldProfilerStats = runtime::RuntimeWorldDiagnostics;
using ProfilerSnapshot = runtime::RuntimeDiagnosticsSnapshot;

class ProfilerHistory {
public:
    explicit ProfilerHistory(std::size_t capacity = 180U);

    void push_frame(double frame_seconds);
    void clear() noexcept;

    [[nodiscard]] std::span<const float> frame_milliseconds() const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] std::size_t capacity() const noexcept;

private:
    std::size_t capacity_ = 180U;
    std::vector<float> frame_milliseconds_;
};

[[nodiscard]] WorldProfilerStats summarize_world(const world::World& world);

[[nodiscard]] ProfilerSnapshot sample_profiler(
    double frame_seconds,
    const jobs::Scheduler& jobs,
    const simulation::Coordinator* simulation,
    const render::InstrumentedDevice& render_device,
    const world::World& world
);

} // namespace vice::editor
