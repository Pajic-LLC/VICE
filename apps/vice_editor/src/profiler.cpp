#include <vice/editor/profiler.hpp>

#include <algorithm>
#include <cmath>

namespace vice::editor {

ProfilerHistory::ProfilerHistory(const std::size_t capacity)
    : capacity_{std::max<std::size_t>(1U, capacity)} {
    frame_milliseconds_.reserve(capacity_);
}

void ProfilerHistory::push_frame(const double frame_seconds) {
    if (!std::isfinite(frame_seconds) || frame_seconds <= 0.0) return;
    if (frame_milliseconds_.size() == capacity_) {
        frame_milliseconds_.erase(frame_milliseconds_.begin());
    }
    frame_milliseconds_.push_back(static_cast<float>(frame_seconds * 1000.0));
}

void ProfilerHistory::clear() noexcept {
    frame_milliseconds_.clear();
}

std::span<const float> ProfilerHistory::frame_milliseconds() const noexcept {
    return frame_milliseconds_;
}

std::size_t ProfilerHistory::size() const noexcept {
    return frame_milliseconds_.size();
}

std::size_t ProfilerHistory::capacity() const noexcept {
    return capacity_;
}

WorldProfilerStats summarize_world(const world::World& world) {
    return runtime::summarize_runtime_world(world);
}

ProfilerSnapshot sample_profiler(
    const double frame_seconds,
    const jobs::Scheduler& jobs,
    const simulation::Coordinator* simulation,
    const render::InstrumentedDevice& render_device,
    const world::World& world
) {
    return runtime::sample_runtime_diagnostics(
        frame_seconds,
        jobs,
        simulation,
        render_device,
        world
    );
}

} // namespace vice::editor
