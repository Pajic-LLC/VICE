#pragma once

#include <vice/project/workspace.hpp>

#include <cstddef>
#include <filesystem>
#include <string>

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

[[nodiscard]] ProjectAssetCookResult cook_project_assets(
    const project::Workspace& workspace
);

} // namespace vice::tools
