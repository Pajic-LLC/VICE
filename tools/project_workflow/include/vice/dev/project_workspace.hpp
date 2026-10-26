#pragma once

#include <vice/project/project.hpp>

#include <filesystem>
#include <optional>
#include <string>

namespace vice::dev {

struct ProjectWorkspace {
    project::Manifest manifest;
    std::filesystem::path manifest_path;
    std::filesystem::path asset_root;
    std::filesystem::path startup_world_source;
    std::filesystem::path cooked_root;
    std::filesystem::path cooked_startup_world;
    std::filesystem::path game_output_root;
};

struct ProjectWorkspaceOpenResult {
    std::optional<ProjectWorkspace> workspace;
    std::string diagnostics;

    [[nodiscard]] explicit operator bool() const noexcept {
        return workspace.has_value();
    }
};

[[nodiscard]] ProjectWorkspaceOpenResult open_project_workspace(
    const std::filesystem::path& manifest_path,
    const std::filesystem::path& cooked_cache_base
);

} // namespace vice::dev
