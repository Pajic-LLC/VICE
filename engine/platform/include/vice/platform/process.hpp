#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace vice::platform {

struct ProcessLaunchRequest {
    std::filesystem::path executable;
    std::vector<std::string> arguments;
    std::filesystem::path working_directory;
};

struct ProcessLaunchResult {
    bool launched = false;
    std::string diagnostics;

    [[nodiscard]] explicit operator bool() const noexcept {
        return launched;
    }
};

[[nodiscard]] ProcessLaunchResult launch_process(const ProcessLaunchRequest& request);

} // namespace vice::platform
