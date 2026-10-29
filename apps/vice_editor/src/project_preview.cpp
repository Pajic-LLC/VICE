#include <vice/editor/project_preview.hpp>
#include <vice/project/workspace.hpp>
#include <vice/tools/project_workflow.hpp>
#include <vice/tools/presentation_cooker.hpp>
#include <vice/tools/project_mesh_cook.hpp>
namespace vice::editor {
bool reset_project_preview(PreviewRuntime& preview, const ProjectSession& session,
    const world::World& authored_world, jobs::Scheduler& jobs, const AssetIndex& assets, std::string* diagnostics) {
    const auto workspace = project::open_workspace(session.manifest_path(), session.cooked_root().parent_path());
    if (!workspace) { if (diagnostics) *diagnostics = workspace.diagnostics; return false; }
    const auto presentation=tools::cook_project_presentation(*workspace.workspace);
    if(!presentation){if(diagnostics)*diagnostics=presentation.diagnostics;return false;}
    const auto meshes=tools::cook_project_meshes(*workspace.workspace);
    if(!meshes){if(diagnostics)*diagnostics=meshes.diagnostics;return false;}
    const auto built = tools::build_project_game_module(*workspace.workspace);
    if (!built) { if (diagnostics) *diagnostics = built.diagnostics; return false; }
    const PreviewGameConfig config{session.manifest(), built.source_present ? built.output : std::filesystem::path{},session.cooked_root(),true};
    return preview.reset(authored_world, jobs, assets, diagnostics, &config);
}
}
