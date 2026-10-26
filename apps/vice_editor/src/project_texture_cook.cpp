#include <vice/editor/project_texture_cook.hpp>

#include <vice/project/workspace.hpp>

namespace vice::editor {

ProjectTextureCookResult cook_project_textures(const ProjectSession& session) {
    return tools::cook_project_textures(project::Workspace{
        .manifest = session.manifest(),
        .manifest_path = session.manifest_path(),
        .asset_root = session.asset_root(),
        .startup_world_source = session.startup_world_source(),
        .cooked_root = session.cooked_root(),
        .cooked_startup_world = session.cooked_startup_world(),
        .game_output_root = session.game_output_root(),
    });
}

} // namespace vice::editor
