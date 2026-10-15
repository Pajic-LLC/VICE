#pragma once

#include <string_view>

namespace vice::core {

enum class BuildConfiguration {
    Debug,
    Release,
};

[[nodiscard]] constexpr BuildConfiguration current_build_configuration() noexcept {
#ifdef NDEBUG
    return BuildConfiguration::Release;
#else
    return BuildConfiguration::Debug;
#endif
}

[[nodiscard]] std::string_view build_configuration_name(BuildConfiguration configuration) noexcept;
[[nodiscard]] std::string_view compiler_name() noexcept;
[[nodiscard]] std::string_view architecture_name() noexcept;

} // namespace vice::core
