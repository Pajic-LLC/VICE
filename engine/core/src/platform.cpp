#include <vice/core/platform.hpp>

namespace vice::core {

std::string_view platform_name(const Platform platform) noexcept {
    switch (platform) {
    case Platform::macOS:
        return "macOS";
    case Platform::Windows:
        return "Windows";
    case Platform::Unsupported:
        return "Unsupported";
    }

    return "Unsupported";
}

} // namespace vice::core
