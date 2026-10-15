#pragma once

#include <cstdint>

namespace vice::platform {

using WindowId = std::uint32_t;
inline constexpr WindowId invalid_window_id = 0;

struct WindowMetrics {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t drawable_width = 0;
    std::uint32_t drawable_height = 0;
    float scale_factor = 1.0F;

    friend constexpr bool operator==(const WindowMetrics&, const WindowMetrics&) noexcept = default;
};

} // namespace vice::platform
