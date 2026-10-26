#include <vice/runtime/profile.hpp>

#include <algorithm>
#include <cmath>

namespace vice::runtime {
namespace {

[[nodiscard]] double finite_non_negative(const double value) noexcept {
    return std::isfinite(value) && value >= 0.0 ? value : 0.0;
}

} // namespace

void RuntimeProfileHistory::push(RuntimeFrameProfileSample sample) noexcept {
    sample.frame_seconds = finite_non_negative(sample.frame_seconds);
    sample.update_seconds = finite_non_negative(sample.update_seconds);
    sample.render_seconds = finite_non_negative(sample.render_seconds);

    frames_[next_] = sample;
    next_ = (next_ + 1U) % runtime_profile_frame_capacity;
    count_ = std::min(count_ + 1U, runtime_profile_frame_capacity);
}

void RuntimeProfileHistory::clear() noexcept {
    frames_ = {};
    count_ = 0U;
    next_ = 0U;
}

RuntimeProfileWindow RuntimeProfileHistory::snapshot() const noexcept {
    RuntimeProfileWindow result;
    result.count = count_;
    if (count_ == 0U) return result;

    const std::size_t first = count_ == runtime_profile_frame_capacity ? next_ : 0U;
    for (std::size_t index = 0; index < count_; ++index) {
        result.frames[index] = frames_[(first + index) % runtime_profile_frame_capacity];
    }
    return result;
}

} // namespace vice::runtime
