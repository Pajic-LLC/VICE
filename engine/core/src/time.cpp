#include <vice/core/time.hpp>

namespace vice::core {

TimePoint monotonic_now() noexcept {
    return MonotonicClock::now();
}

FrameClock::FrameClock(const TimePoint start) noexcept
    : start_{start}, last_{start} {
}

void FrameClock::reset(const TimePoint now) noexcept {
    start_ = now;
    last_ = now;
    delta_ = TimeDuration::zero();
    elapsed_ = TimeDuration::zero();
    frame_index_ = 0;
}

void FrameClock::tick(TimePoint now) noexcept {
    if (now < last_) {
        now = last_;
    }

    delta_ = now - last_;
    elapsed_ = now - start_;
    last_ = now;
    ++frame_index_;
}

void FrameClock::tick() noexcept {
    tick(monotonic_now());
}

TimeDuration FrameClock::delta() const noexcept {
    return delta_;
}

TimeDuration FrameClock::elapsed() const noexcept {
    return elapsed_;
}

double FrameClock::delta_seconds() const noexcept {
    return std::chrono::duration<double>{delta_}.count();
}

double FrameClock::elapsed_seconds() const noexcept {
    return std::chrono::duration<double>{elapsed_}.count();
}

std::uint64_t FrameClock::frame_index() const noexcept {
    return frame_index_;
}

} // namespace vice::core
