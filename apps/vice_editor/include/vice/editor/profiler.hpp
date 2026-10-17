#pragma once

#include <vice/jobs/jobs.hpp>
#include <vice/render/telemetry.hpp>
#include <vice/simulation/simulation.hpp>
#include <vice/world/world.hpp>

#include <cstddef>
#include <optional>
#include <span>
#include <vector>

namespace vice::editor {

struct WorldProfilerStats {
    std::size_t entities = 0;
    std::size_t resources = 0;
    std::size_t relations = 0;
    std::size_t regions = 0;
    std::size_t full_regions = 0;
    std::size_t reduced_regions = 0;
    std::size_t inactive_regions = 0;
};

struct ProfilerSnapshot {
    double frame_seconds = 0.0;
    double frames_per_second = 0.0;
    jobs::SchedulerStats jobs{};
    std::optional<simulation::CoordinatorStats> simulation;
    render::DeviceStats render{};
    WorldProfilerStats world{};
};

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
