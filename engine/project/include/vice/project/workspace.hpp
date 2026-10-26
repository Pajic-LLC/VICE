#pragma once

#include <vice/project/project.hpp>

#include <filesystem>
#include <optional>
#include <string>

namespace vice::project {

struct Workspace {
    Manifest manifest;
    std::filesystem::path manifest_path;
    std::filesystem::path asset_root;
    std::filesystem::path startup_world_source;
    std::filesystem::path cooked_root;
    std::filesystem::path cooked_startup_world;
    std::filesystem::path game_output_root;
};

struct WorkspaceOpenResult {
    std::optional<Workspace> workspace;
    std::string diagnostics;

    [[nodiscard]] explicit operator bool() const noexcept {
        return workspace.has_value();
    }
};

[[nodiscard]] WorkspaceOpenResult open_workspace(
    const std::filesystem::path& manifest_path,
    const std::filesystem::path& cooked_cache_base
);

} // namespace vice::project
