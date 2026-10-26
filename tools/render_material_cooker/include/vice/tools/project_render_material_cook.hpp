#pragma once

#include <vice/project/workspace.hpp>
#include <vice/world/resource.hpp>

#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace vice::tools {

struct ProjectRenderMaterialCookRecord {
    std::filesystem::path relative_path;
    std::filesystem::path cooked_path;
    std::filesystem::path shader_source;
    std::filesystem::path vertex_manifest;
    std::filesystem::path fragment_manifest;
};

struct ProjectRenderMaterialCookResult {
    bool success = false;
    std::vector<ProjectRenderMaterialCookRecord> materials;
    std::string diagnostics;

    [[nodiscard]] explicit operator bool() const noexcept { return success; }
};

[[nodiscard]] ProjectRenderMaterialCookResult cook_project_render_materials(
    const project::Workspace& workspace,
    std::span<const world::ResourceId> available_textures = {}
);

} // namespace vice::tools
