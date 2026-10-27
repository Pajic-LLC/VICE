#include <vice/editor/project_preview.hpp>
#include <vice/project/workspace.hpp>
#include <vice/tools/project_workflow.hpp>
namespace vice::editor {
bool reset_project_preview(PreviewRuntime& preview, const ProjectSession& session,
    const world::World& authored_world, jobs::Scheduler& jobs, const AssetIndex& assets, std::string* diagnostics) {
    const auto workspace = project::open_workspace(session.manifest_path(), session.cooked_root().parent_path());
    if (!workspace) { if (diagnostics) *diagnostics = workspace.diagnostics; return false; }
    const auto built = tools::build_project_game_module(*workspace.workspace);
    if (!built) { if (diagnostics) *diagnostics = built.diagnostics; return false; }
    const PreviewGameConfig config{session.manifest(), built.source_present ? built.output : std::filesystem::path{}};
    return preview.reset(authored_world, jobs, assets, diagnostics, &config);
}
}
