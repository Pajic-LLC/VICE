#include <vice/editor/project_material_cook.hpp>

#include <vice/project/workspace.hpp>

namespace vice::editor {

ProjectMaterialCookResult cook_project_render_materials(
    const ProjectSession& session,
    const std::span<const world::ResourceId> available_textures
) {
    return tools::cook_project_render_materials(project::Workspace{
        .manifest = session.manifest(),
        .manifest_path = session.manifest_path(),
        .asset_root = session.asset_root(),
        .startup_world_source = session.startup_world_source(),
        .cooked_root = session.cooked_root(),
        .cooked_startup_world = session.cooked_startup_world(),
        .game_output_root = session.game_output_root(),
    }, available_textures);
}

} // namespace vice::editor
