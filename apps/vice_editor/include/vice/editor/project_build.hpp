#pragma once

#include <vice/editor/project_session.hpp>
#include <vice/platform/process.hpp>

#include <cstddef>
#include <filesystem>
#include <string>

namespace vice::editor {

struct ProjectBuildResult {
    bool success = false;
    std::filesystem::path cooked_startup_world;
    std::size_t entity_count = 0;
    std::size_t region_count = 0;
    std::string diagnostics;

    [[nodiscard]] explicit operator bool() const noexcept {
        return success;
    }
};

struct ProjectRunResult {
    bool launched = false;
    ProjectBuildResult build;
    std::string diagnostics;

    [[nodiscard]] explicit operator bool() const noexcept {
        return launched;
    }
};

[[nodiscard]] ProjectBuildResult build_project(const ProjectSession& session);

[[nodiscard]] platform::ProcessLaunchRequest project_run_request(
    const ProjectSession& session,
    const std::filesystem::path& player_executable
);

[[nodiscard]] ProjectRunResult run_project(
    const ProjectSession& session,
    const std::filesystem::path& player_executable
);

} // namespace vice::editor
