#pragma once

#include <vice/project/workspace.hpp>

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace vice::tools {

struct ProjectAssetCookResult {
    bool success = false;
    std::filesystem::path cooked_startup_world;
    std::size_t entity_count = 0U;
    std::size_t region_count = 0U;
    std::size_t mesh_count = 0U;
    std::size_t texture_count = 0U;
    std::size_t environment_count = 0U;
    std::size_t material_count = 0U;
    std::string diagnostics;

    [[nodiscard]] explicit operator bool() const noexcept { return success; }
};

struct ProjectBuildToolchain {
    std::filesystem::path cmake_executable;
    std::filesystem::path game_module_cmake_source;
    std::filesystem::path game_module_include;

    [[nodiscard]] bool game_module_available() const noexcept {
        return !cmake_executable.empty() &&
            !game_module_cmake_source.empty() &&
            !game_module_include.empty();
    }
};

struct ProjectBuildResult {
    bool success = false;
    std::string project_id;
    std::string game_target;
    ProjectAssetCookResult assets;
    std::filesystem::path game_module_source;
    std::filesystem::path game_module;
    std::filesystem::path game_output_root;
    std::filesystem::path game_executable;
    std::filesystem::path runtime_manifest;
    std::filesystem::path runtime_content_root;
    std::string diagnostics;

    [[nodiscard]] explicit operator bool() const noexcept { return success; }
};

struct ProjectRuntimeSession {
    std::string id;
    std::filesystem::path root;
};

struct ProjectLaunchRequest {
    std::filesystem::path executable;
    std::vector<std::string> arguments;
    std::filesystem::path working_directory;
};

struct ProjectRunResult {
    bool launched = false;
    ProjectBuildResult build;
    std::optional<ProjectRuntimeSession> runtime_session;
    std::string diagnostics;

    [[nodiscard]] explicit operator bool() const noexcept { return launched; }
};

[[nodiscard]] ProjectAssetCookResult cook_project_assets(
    const project::Workspace& workspace
);

[[nodiscard]] ProjectBuildToolchain configured_project_build_toolchain();

[[nodiscard]] ProjectBuildResult build_project(
    const project::Workspace& workspace,
    const std::filesystem::path& runtime_host_executable,
    const ProjectBuildToolchain& toolchain
);

[[nodiscard]] ProjectBuildResult build_project(
    const project::Workspace& workspace,
    const std::filesystem::path& runtime_host_executable
);

[[nodiscard]] ProjectLaunchRequest project_launch_request(
    const ProjectBuildResult& build
);

[[nodiscard]] ProjectLaunchRequest project_launch_request(
    const ProjectBuildResult& build,
    const ProjectRuntimeSession& runtime_session
);

[[nodiscard]] ProjectRunResult run_project(
    const project::Workspace& workspace,
    const std::filesystem::path& runtime_host_executable,
    const ProjectBuildToolchain& toolchain
);

[[nodiscard]] ProjectRunResult run_project(
    const project::Workspace& workspace,
    const std::filesystem::path& runtime_host_executable
);

[[nodiscard]] ProjectRunResult run_project_with_session(
    const project::Workspace& workspace,
    const std::filesystem::path& runtime_host_executable,
    const ProjectBuildToolchain& toolchain,
    ProjectRuntimeSession runtime_session
);

[[nodiscard]] ProjectRunResult run_project_with_session(
    const project::Workspace& workspace,
    const std::filesystem::path& runtime_host_executable,
    ProjectRuntimeSession runtime_session
);

} // namespace vice::tools
