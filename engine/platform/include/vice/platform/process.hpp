#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace vice::platform {

struct ProcessLaunchRequest {
    std::filesystem::path executable;
    std::vector<std::string> arguments;
    std::filesystem::path working_directory;
    // Asynchronous launches may discard inherited streams. run_process captures
    // output regardless of this flag.
    bool inherit_standard_streams = true;
};

struct ProcessLaunchResult {
    bool launched = false;
    std::string diagnostics;

    [[nodiscard]] explicit operator bool() const noexcept {
        return launched;
    }
};

struct ProcessRunResult {
    bool launched = false;
    int exit_code = -1;
    std::string output;
    std::string diagnostics;

    [[nodiscard]] explicit operator bool() const noexcept {
        return launched && exit_code == 0;
    }
};

[[nodiscard]] ProcessLaunchResult launch_process(const ProcessLaunchRequest& request);
[[nodiscard]] ProcessRunResult run_process(const ProcessLaunchRequest& request);

} // namespace vice::platform
