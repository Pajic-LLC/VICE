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
    const auto snapshot = world.snapshot(world::ComponentPayloadSnapshot::render);
    WorldProfilerStats result;
    result.entities = snapshot.entities.size();
    result.resources = snapshot.resources.size();
    result.relations = world.relations().size();
    result.regions = snapshot.regions.size();
    for (const auto& region : snapshot.regions) {
        switch (region.fidelity) {
        case world::RegionFidelity::full: ++result.full_regions; break;
        case world::RegionFidelity::reduced: ++result.reduced_regions; break;
        case world::RegionFidelity::inactive: ++result.inactive_regions; break;
        }
    }
    return result;
}

ProfilerSnapshot sample_profiler(
    const double frame_seconds,
    const jobs::Scheduler& jobs,
    const simulation::Coordinator* simulation,
    const render::InstrumentedDevice& render_device,
    const world::World& world
) {
    ProfilerSnapshot result;
    if (std::isfinite(frame_seconds) && frame_seconds > 0.0) {
        result.frame_seconds = frame_seconds;
        result.frames_per_second = 1.0 / frame_seconds;
    }
    result.jobs = jobs.stats();
    if (simulation != nullptr) result.simulation = simulation->stats();
    result.render = render_device.stats();
    result.world = summarize_world(world);
    return result;
}

} // namespace vice::editor
