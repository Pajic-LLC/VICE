#include <vice/editor/project_settings.hpp>

#include <vice/project/project.hpp>

#include <filesystem>
#include <string>
#include <utility>

namespace vice::editor {
namespace {

[[nodiscard]] bool fail(std::string* const diagnostics, std::string message) {
    if (diagnostics != nullptr) *diagnostics = std::move(message);
    return false;
}

[[nodiscard]] bool path_within(
    const std::filesystem::path& child,
    const std::filesystem::path& parent
) {
    auto child_it = child.begin();
    auto parent_it = parent.begin();
    for (; parent_it != parent.end(); ++parent_it, ++child_it) {
        if (child_it == child.end() || *child_it != *parent_it) return false;
    }
    return true;
}

} // namespace

bool save_project_settings(
    ProjectSession& session,
    const ProjectSettingsUpdate& update,
    std::string* const diagnostics
) {
    project::Manifest candidate = session.manifest();
    candidate.name = update.project_name;
    candidate.game_name = update.game_name;
    candidate.startup_world = update.startup_world;

    std::string validation;
    if (!project::validate(candidate, &validation)) return fail(diagnostics, std::move(validation));

    const auto startup_source = project::resolve_startup_world(session.manifest_path(), candidate);
    if (startup_source.empty()) return fail(diagnostics, "Project startup World could not be resolved.");

    std::error_code error;
    const auto canonical_asset_root = std::filesystem::weakly_canonical(session.asset_root(), error);
    if (error) {
        return fail(diagnostics, "Could not resolve the project asset root: " + error.message());
    }
    const auto canonical_startup = std::filesystem::weakly_canonical(startup_source, error);
    if (error) {
        return fail(diagnostics, "Could not resolve the selected startup World: " + error.message());
    }
    if (!path_within(canonical_startup, canonical_asset_root)) {
        return fail(diagnostics, "Startup World must belong to this project's asset root.");
    }
    if (!std::filesystem::is_regular_file(canonical_startup, error) || error) {
        return fail(
            diagnostics,
            error
                ? "Could not inspect the selected startup World: " + error.message()
                : "Selected startup World does not exist: " + startup_source.generic_string()
        );
    }

    if (!project::save_manifest(session.manifest_path(), candidate, diagnostics)) return false;
    if (!session.apply_manifest(std::move(candidate), diagnostics)) {
        return fail(diagnostics, "Project settings were saved but could not be applied to the open session.");
    }

    if (diagnostics != nullptr) diagnostics->clear();
    return true;
}

} // namespace vice::editor
