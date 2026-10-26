#include <vice/editor/project_mesh_cook.hpp>

#include <vice/project/workspace.hpp>

namespace vice::editor {

ProjectMeshCookResult cook_project_meshes(const ProjectSession& session) {
    return dev::cook_project_meshes(project::Workspace{
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
