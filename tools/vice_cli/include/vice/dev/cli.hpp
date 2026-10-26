#pragma once

#include <iterator>
#include <span>
#include <string>
#include <string_view>

namespace vice::dev {

inline constexpr std::string_view structured_schema = "vice.dev.v1";

inline constexpr int exit_success = 0;
inline constexpr int exit_usage = 2;
inline constexpr int exit_validation = 3;
inline constexpr int exit_unavailable = 4;

struct CommandResult {
    int exit_code = exit_success;
    std::string json;
};

[[nodiscard]] CommandResult execute(std::span<const std::string_view> arguments);

} // namespace vice::dev
