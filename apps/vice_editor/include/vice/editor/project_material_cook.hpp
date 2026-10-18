#pragma once

#include <vice/editor/project_session.hpp>

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace vice::editor {

struct CookedProjectRenderMaterial {
    std::filesystem::path relative_path;
    std::filesystem::path cooked_path;
    std::filesystem::path shader_source;
    std::filesystem::path vertex_manifest;
    std::filesystem::path fragment_manifest;
};

struct ProjectMaterialCookResult {
    bool success = false;
    std::vector<CookedProjectRenderMaterial> materials;
    std::string diagnostics;

    [[nodiscard]] explicit operator bool() const noexcept {
        return success;
    }
};

[[nodiscard]] ProjectMaterialCookResult cook_project_render_materials(
    const ProjectSession& session
);

} // namespace vice::editor
