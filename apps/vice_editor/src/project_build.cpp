#include <vice/editor/project_build.hpp>

#include <vice/project/workspace.hpp>
#include <vice/tools/project_workflow.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <utility>

namespace vice::editor {
namespace {

[[nodiscard]] std::string path_text(const std::filesystem::path& path) {
    const auto value = path.generic_u8string();
    return std::string{
        reinterpret_cast<const char*>(value.data()),
        value.size()
    };
}

[[nodiscard]] ProjectBuildResult fail_build(std::string diagnostics) {
    ProjectBuildResult result;
    result.diagnostics = std::move(diagnostics);
    return result;
}

[[nodiscard]] std::optional<project::Workspace> persisted_workspace(
    const ProjectSession& session,
    std::string& diagnostics
) {
    const auto opened = project::open_workspace(
        session.manifest_path(),
        session.cooked_root().parent_path()
    );
    if (!opened) {
        diagnostics = opened.diagnostics;
        return std::nullopt;
    }
    diagnostics.clear();
    return *opened.workspace;
}

[[nodiscard]] ProjectBuildResult adapt_asset_cook(
    const project::Workspace& workspace,
    const tools::ProjectAssetCookResult& cooked
) {
    ProjectBuildResult result;
    result.success = cooked.success;
    result.game_target = workspace.manifest.game_target;
    result.cooked_startup_world = cooked.cooked_startup_world;
    result.entity_count = cooked.entity_count;
    result.region_count = cooked.region_count;
    result.mesh_count = cooked.mesh_count;
    result.texture_count = cooked.texture_count;
    result.environment_count = cooked.environment_count;
    result.material_count = cooked.material_count;
    result.diagnostics = cooked.diagnostics;
    return result;
}

[[nodiscard]] ProjectBuildResult adapt_build(const tools::ProjectBuildResult& shared) {
    ProjectBuildResult result;
    result.success = shared.success;
    result.game_target = shared.game_target;
    result.cooked_startup_world = shared.assets.cooked_startup_world;
    result.game_module_source = shared.game_module_source;
    result.game_module = shared.game_module;
    result.game_output_root = shared.game_output_root;
    result.game_executable = shared.game_executable;
    result.runtime_manifest = shared.runtime_manifest;
    result.runtime_content_root = shared.runtime_content_root;
    result.entity_count = shared.assets.entity_count;
    result.region_count = shared.assets.region_count;
    result.mesh_count = shared.assets.mesh_count;
    result.texture_count = shared.assets.texture_count;
    result.environment_count = shared.assets.environment_count;
    result.material_count = shared.assets.material_count;
    result.diagnostics = shared.diagnostics;
    return result;
}

} // namespace

ProjectBuildResult build_project(const ProjectSession& session) {
#if defined(VICE_EDITOR_RUNTIME_HOST_EXECUTABLE)
    return build_game_target(
        session,
        std::filesystem::path{VICE_EDITOR_RUNTIME_HOST_EXECUTABLE}
    );
#else
    std::string diagnostics;
    const auto workspace = persisted_workspace(session, diagnostics);
    if (!workspace.has_value()) {
        return fail_build(std::move(diagnostics));
    }
    return adapt_asset_cook(*workspace, tools::cook_project_assets(*workspace));
#endif
}

ProjectBuildResult build_game_target(
    const ProjectSession& session,
    const std::filesystem::path& runtime_host_executable
) {
    std::string diagnostics;
    const auto workspace = persisted_workspace(session, diagnostics);
    if (!workspace.has_value()) {
        return fail_build(std::move(diagnostics));
    }
    return adapt_build(tools::build_project(*workspace, runtime_host_executable));
}

platform::ProcessLaunchRequest project_run_request(
    const ProjectSession& session,
    const std::filesystem::path& player_executable
) {
    platform::ProcessLaunchRequest request;
    request.executable = player_executable;
    request.arguments.push_back(path_text(session.manifest_path()));
    request.arguments.push_back(path_text(session.cooked_root().parent_path()));
    request.working_directory = session.manifest_path().parent_path();
    return request;
}

platform::ProcessLaunchRequest game_run_request(const ProjectBuildResult& build) {
    platform::ProcessLaunchRequest request;
    request.executable = build.game_executable;
    request.working_directory = build.game_output_root;
    return request;
}

ProjectRunResult run_project(
    const ProjectSession& session,
    const std::filesystem::path& runtime_host_executable
) {
    ProjectRunResult result;

    std::string diagnostics;
    const auto workspace = persisted_workspace(session, diagnostics);
    if (!workspace.has_value()) {
        result.diagnostics = std::move(diagnostics);
        return result;
    }

    const auto shared = tools::run_project(*workspace, runtime_host_executable);
    result.build = adapt_build(shared.build);
    result.launched = shared.launched;
    result.diagnostics = shared.diagnostics;
    return result;
}

} // namespace vice::editor
