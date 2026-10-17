#include <vice/editor/project_session.hpp>

#include <vice/project/project.hpp>

#include <filesystem>
#include <string>
#include <utility>

namespace vice::editor {

bool ProjectSession::set_startup_world(
    const std::filesystem::path& asset_relative_world,
    std::string* const diagnostics
) {
    if (!project::valid_relative_path(asset_relative_world)) {
        if (diagnostics != nullptr) {
            *diagnostics = "Startup World must be a portable path relative to the project asset root.";
        }
        return false;
    }
    if (asset_relative_world.extension() != ".vworld") {
        if (diagnostics != nullptr) *diagnostics = "Startup World must be an authored .vworld asset.";
        return false;
    }

    const auto source = (asset_root_ / asset_relative_world).lexically_normal();
    std::error_code error;
    if (!std::filesystem::is_regular_file(source, error) || error) {
        if (diagnostics != nullptr) {
            *diagnostics = error
                ? "Could not inspect startup World: " + error.message()
                : "Startup World does not exist inside the project asset root.";
        }
        return false;
    }

    auto candidate = manifest_;
    candidate.startup_world = asset_relative_world;
    std::string save_diagnostics;
    if (!project::save_manifest(manifest_path_, candidate, &save_diagnostics)) {
        if (diagnostics != nullptr) *diagnostics = save_diagnostics;
        return false;
    }

    manifest_ = std::move(candidate);
    startup_world_source_ = source;
    auto cooked_relative = asset_relative_world;
    cooked_relative.replace_extension(".vworldc");
    cooked_startup_world_ = (cooked_root_ / cooked_relative).lexically_normal();
    if (diagnostics != nullptr) diagnostics->clear();
    return true;
}

} // namespace vice::editor
