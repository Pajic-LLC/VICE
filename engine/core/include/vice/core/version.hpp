#pragma once

#include <string_view>

namespace vice::core {

struct Version {
    static constexpr int major = 0;
    static constexpr int minor = 1;
    static constexpr int patch = 0;
};

[[nodiscard]] std::string_view version_string() noexcept;

} // namespace vice::core
