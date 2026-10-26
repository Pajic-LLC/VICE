#pragma once

#include <vice/project/workspace.hpp>

#include <filesystem>
#include <string>
#include <vector>

namespace vice::dev {

struct CookedProjectMesh {
    std::filesystem::path relative_path;
    std::filesystem::path cooked_path;
};

struct ProjectMeshCookResult {
    bool success = false;
    std::vector<CookedProjectMesh> meshes;
    std::string diagnostics;

    [[nodiscard]] explicit operator bool() const noexcept { return success; }
};

[[nodiscard]] ProjectMeshCookResult cook_project_meshes(const project::Workspace& workspace);

} // namespace vice::dev
