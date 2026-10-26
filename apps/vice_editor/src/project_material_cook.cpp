#include <vice/editor/project_material_cook.hpp>

namespace vice::editor {

ProjectMaterialCookResult cook_project_render_materials(
    const ProjectSession& session,
    const std::span<const world::ResourceId> available_textures
) {
    const auto* workspace = session.workspace();
    if (workspace == nullptr) {
        ProjectMaterialCookResult result;
        result.diagnostics = "No project workspace is open.";
        return result;
    }
    return tools::cook_project_render_materials(*workspace, available_textures);
}

} // namespace vice::editor
