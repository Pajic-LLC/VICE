#include <vice/dev/project_workspace.hpp>

#include <filesystem>
#include <string>
#include <system_error>

namespace vice::dev {
namespace {

[[nodiscard]] std::filesystem::path absolute_normalized(
    const std::filesystem::path& path,
    std::error_code& error
) {
    if (path.empty()) {
        error = std::make_error_code(std::errc::invalid_argument);
        return {};
    }
    auto result = std::filesystem::absolute(path, error);
    if (error) return {};
    return result.lexically_normal();
}

[[nodiscard]] ProjectWorkspaceOpenResult fail(std::string diagnostics) {
    ProjectWorkspaceOpenResult result;
    result.diagnostics = std::move(diagnostics);
    return result;
}

} // namespace

ProjectWorkspaceOpenResult open_project_workspace(
    const std::filesystem::path& manifest_path,
    const std::filesystem::path& cooked_cache_base
) {
    std::error_code error;
    const auto resolved_manifest = absolute_normalized(manifest_path, error);
    if (error) {
        return fail("Could not resolve project manifest path: " + error.message());
    }

    const auto loaded = project::load_manifest(resolved_manifest);
    if (!loaded.manifest.has_value()) {
        return fail(loaded.diagnostics);
    }

    const auto resolved_cache = absolute_normalized(cooked_cache_base, error);
    if (error) {
        return fail("Could not resolve project cooked-cache root: " + error.message());
    }

    ProjectWorkspace workspace;
    workspace.manifest = *loaded.manifest;
    workspace.manifest_path = resolved_manifest;
    workspace.asset_root = project::resolve_asset_root(resolved_manifest, workspace.manifest);
    workspace.startup_world_source = project::resolve_startup_world(resolved_manifest, workspace.manifest);
    workspace.cooked_root = project::resolve_cooked_root(resolved_cache, workspace.manifest);
    workspace.cooked_startup_world = project::resolve_cooked_startup_world(
        resolved_cache,
        workspace.manifest
    );
    workspace.game_output_root = project::resolve_game_output_root(
        resolved_manifest,
        workspace.manifest
    );

    if (workspace.asset_root.empty() || workspace.startup_world_source.empty() ||
        workspace.cooked_root.empty() || workspace.cooked_startup_world.empty() ||
        workspace.game_output_root.empty()) {
        return fail("Project asset, cooked, startup World, or game output paths could not be resolved.");
    }

    ProjectWorkspaceOpenResult result;
    result.workspace = std::move(workspace);
    return result;
}

} // namespace vice::dev
