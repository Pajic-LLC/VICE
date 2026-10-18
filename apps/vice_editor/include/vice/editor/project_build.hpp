#pragma once

#include <vice/editor/project_session.hpp>
#include <vice/platform/process.hpp>

#include <cstddef>
#include <filesystem>
#include <string>

namespace vice::editor {

struct ProjectBuildResult {
    bool success = false;
    std::string game_target;
    std::filesystem::path cooked_startup_world;
    std::filesystem::path game_module_source;
    std::filesystem::path game_module;
    std::filesystem::path game_output_root;
    std::filesystem::path game_executable;
    std::filesystem::path runtime_manifest;
    std::filesystem::path runtime_content_root;
    std::size_t entity_count = 0;
    std::size_t region_count = 0;
    std::size_t mesh_count = 0;
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

[[nodiscard]] ProjectBuildResult build_game_target(
    const ProjectSession& session,
    const std::filesystem::path& runtime_host_executable
);

[[nodiscard]] platform::ProcessLaunchRequest project_run_request(
    const ProjectSession& session,
    const std::filesystem::path& player_executable
);

[[nodiscard]] platform::ProcessLaunchRequest game_run_request(const ProjectBuildResult& build);

[[nodiscard]] ProjectRunResult run_project(
    const ProjectSession& session,
    const std::filesystem::path& runtime_host_executable
);

} // namespace vice::editor
