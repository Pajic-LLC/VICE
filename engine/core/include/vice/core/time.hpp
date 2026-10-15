#pragma once

#include <chrono>
#include <cstdint>

namespace vice::core {

using MonotonicClock = std::chrono::steady_clock;
using TimePoint = MonotonicClock::time_point;
using TimeDuration = MonotonicClock::duration;

[[nodiscard]] TimePoint monotonic_now() noexcept;

class FrameClock {
public:
    explicit FrameClock(TimePoint start = monotonic_now()) noexcept;

    void reset(TimePoint now = monotonic_now()) noexcept;
    void tick(TimePoint now) noexcept;
    void tick() noexcept;

    [[nodiscard]] TimeDuration delta() const noexcept;
    [[nodiscard]] TimeDuration elapsed() const noexcept;
    [[nodiscard]] double delta_seconds() const noexcept;
    [[nodiscard]] double elapsed_seconds() const noexcept;
    [[nodiscard]] std::uint64_t frame_index() const noexcept;

private:
    TimePoint start_;
    TimePoint last_;
    TimeDuration delta_{};
    TimeDuration elapsed_{};
    std::uint64_t frame_index_ = 0;
};

} // namespace vice::core
