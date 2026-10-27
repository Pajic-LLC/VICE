#pragma once

#include <filesystem>
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
inline constexpr int exit_build = 5;
inline constexpr int exit_launch = 6;

struct CommandResult {
    int exit_code = exit_success;
    std::string json;
};

struct CommandEnvironment {
    std::filesystem::path runtime_host_executable;
    std::filesystem::path runtime_session_root;
    std::string runtime_session_id;
};

[[nodiscard]] CommandEnvironment configured_command_environment();

[[nodiscard]] CommandResult execute_runtime_profile(
    const std::filesystem::path& runtime_session_path
);

[[nodiscard]] CommandResult execute_runtime_debug(
    const std::filesystem::path& runtime_session_path,
    std::string_view entity_id
);

[[nodiscard]] CommandResult execute_world_edit(
    const std::filesystem::path& world_path,
    const std::filesystem::path& request_path,
    bool dry_run
);

[[nodiscard]] CommandResult execute(
    std::span<const std::string_view> arguments,
    const CommandEnvironment& environment
);

[[nodiscard]] CommandResult execute(std::span<const std::string_view> arguments);

} // namespace vice::dev
