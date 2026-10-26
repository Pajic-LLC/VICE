#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace vice::runtime {

inline constexpr std::size_t runtime_profile_frame_capacity = 120U;

struct RuntimeFrameProfileSample {
    std::uint64_t frame_index = 0U;
    double frame_seconds = 0.0;
    double update_seconds = 0.0;
    double render_seconds = 0.0;

    friend bool operator==(const RuntimeFrameProfileSample&, const RuntimeFrameProfileSample&) = default;
};

struct RuntimeProfileWindow {
    std::array<RuntimeFrameProfileSample, runtime_profile_frame_capacity> frames{};
    std::size_t count = 0U;

    friend bool operator==(const RuntimeProfileWindow&, const RuntimeProfileWindow&) = default;
};

class RuntimeProfileHistory final {
public:
    void push(RuntimeFrameProfileSample sample) noexcept;
    void clear() noexcept;

    [[nodiscard]] std::size_t size() const noexcept { return count_; }
    [[nodiscard]] RuntimeProfileWindow snapshot() const noexcept;

private:
    std::array<RuntimeFrameProfileSample, runtime_profile_frame_capacity> frames_{};
    std::size_t count_ = 0U;
    std::size_t next_ = 0U;
};

} // namespace vice::runtime
