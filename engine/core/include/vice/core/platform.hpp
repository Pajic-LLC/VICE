#pragma once

#include <string_view>

namespace vice::core {

enum class Platform {
    macOS,
    Windows,
    Unsupported,
};

[[nodiscard]] constexpr Platform current_platform() noexcept {
#if defined(__APPLE__)
    return Platform::macOS;
#elif defined(_WIN32)
    return Platform::Windows;
#else
    return Platform::Unsupported;
#endif
}

[[nodiscard]] std::string_view platform_name(Platform platform) noexcept;

} // namespace vice::core
