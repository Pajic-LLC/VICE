#include <vice/core/build.hpp>

namespace vice::core {

std::string_view build_configuration_name(const BuildConfiguration configuration) noexcept {
    switch (configuration) {
    case BuildConfiguration::Debug:
        return "Debug";
    case BuildConfiguration::Release:
        return "Release";
    }

    return "Unknown";
}

std::string_view compiler_name() noexcept {
#if defined(__clang__)
    return "Clang";
#elif defined(_MSC_VER)
    return "MSVC";
#elif defined(__GNUC__)
    return "GCC";
#else
    return "Unknown";
#endif
}

std::string_view architecture_name() noexcept {
#if defined(__aarch64__) || defined(_M_ARM64)
    return "ARM64";
#elif defined(__x86_64__) || defined(_M_X64)
    return "x64";
#elif defined(__i386__) || defined(_M_IX86)
    return "x86";
#else
    return "Unknown";
#endif
}

} // namespace vice::core
