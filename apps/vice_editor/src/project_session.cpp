#include <vice/editor/project_session.hpp>
#include <vice/editor/project_workflow.hpp>

#include <vice/world/snapshot_file.hpp>

#include <fstream>
#include <random>
#include <system_error>
#include <utility>

namespace vice::editor {
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

[[nodiscard]] bool fail(std::string* const diagnostics, std::string message) {
    if (diagnostics != nullptr) *diagnostics = std::move(message);
    return false;
}

[[nodiscard]] bool portable_project_name(const std::string& name) noexcept {
    if (name.empty() || name == "." || name == "..") return false;
    if (name.front() == ' ' || name.back() == ' ' || name.back() == '.') return false;
    for (const unsigned char value : name) {
        if (value < 0x20U || value == '<' || value == '>' || value == ':' || value == '"' ||
            value == '/' || value == '\\' || value == '|' || value == '?' || value == '*') {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool write_text(
    const std::filesystem::path& path,
    const std::string& text,
    std::string& diagnostics
) {
    std::ofstream output{path, std::ios::binary | std::ios::trunc};
    if (!output) {
        diagnostics = "Could not create `" + path.generic_string() + "`.";
        return false;
    }
    output << text;
    if (!output) {
        diagnostics = "Could not write `" + path.generic_string() + "`.";
        return false;
    }
    return true;
}

} // namespace

project::ProjectId generate_project_id() {
    std::random_device random;
    project::ProjectId result;
    for (auto& byte : result.bytes) {
        byte = static_cast<std::uint8_t>(random());
    }
    result.bytes[6] = static_cast<std::uint8_t>((result.bytes[6] & 0x0FU) | 0x40U);
    result.bytes[8] = static_cast<std::uint8_t>((result.bytes[8] & 0x3FU) | 0x80U);
    if (!result) result.bytes[15] = 1U;
    return result;
}

ProjectCreateResult create_project(const ProjectCreateRequest& request) {
    ProjectCreateResult result;
    if (!portable_project_name(request.name)) {
        result.diagnostics = "Project name contains characters that are not portable between macOS and Windows.";
        return result;
    }

    std::error_code error;
    const auto parent = absolute_normalized(request.parent_directory, error);
    if (error) {
        result.diagnostics = "Could not resolve the project parent folder: " + error.message();
        return result;
    }
    if (!std::filesystem::is_directory(parent, error) || error) {
        result.diagnostics = error
            ? "Could not inspect the project parent folder: " + error.message()
            : "Project parent folder does not exist.";
        return result;
    }

    const project::ProjectId id = request.project_id.value_or(generate_project_id());
    if (!id) {
        result.diagnostics = "Project ID must be non-zero.";
        return result;
    }

    const auto destination = (parent / request.name).lexically_normal();
    if (std::filesystem::exists(destination, error)) {
        result.diagnostics = error
            ? "Could not inspect the project destination: " + error.message()
            : "A file or folder with that project name already exists.";
        return result;
    }
    if (error) {
        result.diagnostics = "Could not inspect the project destination: " + error.message();
        return result;
    }

    const auto staging = (parent / (".vice-create-" + id.to_string())).lexically_normal();
    std::filesystem::remove_all(staging, error);
    error.clear();
    const auto worlds = staging / "assets" / "worlds";
    std::filesystem::create_directories(worlds, error);
    if (error) {
        result.diagnostics = "Could not create the project staging directory: " + error.message();
        std::filesystem::remove_all(staging, error);
        return result;
    }

    project::Manifest manifest;
    manifest.id = id;
    manifest.name = request.name;
    manifest.asset_root = "assets";
    manifest.startup_world = "worlds/main.vworld";
    const std::string manifest_text = project::serialize_manifest(manifest);
    if (manifest_text.empty()) {
        result.diagnostics = "Could not serialize the new project manifest.";
        std::filesystem::remove_all(staging, error);
        return result;
    }

    const std::string world_text =
        "vice-world-source 1\n\n"
        "region 1 1 0 0 0 0 0 0 full bounds -100 -100 -100 100 100 100\n";

    std::string diagnostics;
    if (!write_text(staging / "vice.viceproj", manifest_text, diagnostics) ||
        !write_text(worlds / "main.vworld", world_text, diagnostics)) {
        result.diagnostics = diagnostics;
        std::filesystem::remove_all(staging, error);
        return result;
    }

    std::filesystem::rename(staging, destination, error);
    if (error) {
        result.diagnostics = "Could not finish creating the project: " + error.message();
        std::error_code cleanup_error;
        std::filesystem::remove_all(staging, cleanup_error);
        return result;
    }

    result.manifest_path = destination / "vice.viceproj";
    result.project_id = id;
    return result;
}

std::optional<ProjectSession> ProjectSession::open(
    const std::filesystem::path& manifest_path,
    const std::filesystem::path& cooked_cache_base,
    const std::filesystem::path& editor_state_base,
    std::string* const diagnostics
) {
    std::error_code error;
    const auto resolved_manifest = absolute_normalized(manifest_path, error);
    if (error) {
        if (diagnostics != nullptr) {
            *diagnostics = "Could not resolve project manifest path: " + error.message();
        }
        return std::nullopt;
    }

    const auto loaded = project::load_manifest(resolved_manifest);
    if (!loaded.manifest.has_value()) {
        if (diagnostics != nullptr) *diagnostics = loaded.diagnostics;
        return std::nullopt;
    }

    const auto cooked_base = absolute_normalized(cooked_cache_base, error);
    if (error) {
        if (diagnostics != nullptr) {
            *diagnostics = "Could not resolve editor cooked-cache root: " + error.message();
        }
        return std::nullopt;
    }
    const auto state_base = absolute_normalized(editor_state_base, error);
    if (error) {
        if (diagnostics != nullptr) {
            *diagnostics = "Could not resolve editor state root: " + error.message();
        }
        return std::nullopt;
    }

    ProjectSession session;
    session.manifest_ = *loaded.manifest;
    session.manifest_path_ = resolved_manifest;
    session.asset_root_ = project::resolve_asset_root(resolved_manifest, session.manifest_);
    session.startup_world_source_ = project::resolve_startup_world(resolved_manifest, session.manifest_);
    if (session.asset_root_.empty() || session.startup_world_source_.empty()) {
        if (diagnostics != nullptr) *diagnostics = "Project authored asset paths could not be resolved.";
        return std::nullopt;
    }

    const std::string project_key = session.manifest_.id.to_string();
    session.cooked_root_ = (cooked_base / project_key).lexically_normal();
    session.editor_state_root_ = (state_base / project_key).lexically_normal();
    session.layout_path_ = session.editor_state_root_ / "layout.ini";
    session.working_world_path_ = session.editor_state_root_ / "world.vworldc";

    auto cooked_startup_relative = session.manifest_.startup_world;
    cooked_startup_relative.replace_extension(".vworldc");
    session.cooked_startup_world_ =
        (session.cooked_root_ / cooked_startup_relative).lexically_normal();

    if (diagnostics != nullptr) diagnostics->clear();
    return session;
}

WorldDocument::WorldDocument() : world_{std::make_unique<world::World>()} {}
WorldDocument::~WorldDocument() = default;
WorldDocument::WorldDocument(WorldDocument&&) noexcept = default;
WorldDocument& WorldDocument::operator=(WorldDocument&&) noexcept = default;

bool WorldDocument::replacement_allowed(
    const DirtyReplacePolicy policy,
    std::string* const diagnostics
) const {
    if (!dirty_ || policy == DirtyReplacePolicy::discard) return true;
    return fail(diagnostics, "World document has unsaved changes.");
}

bool WorldDocument::new_empty(
    const DirtyReplacePolicy policy,
    std::string* const diagnostics
) {
    if (!replacement_allowed(policy, diagnostics)) return false;
    world_ = std::make_unique<world::World>();
    path_.clear();
    dirty_ = false;
    if (diagnostics != nullptr) diagnostics->clear();
    return true;
}

bool WorldDocument::open(
    const std::filesystem::path& path,
    const DirtyReplacePolicy policy,
    std::string* const diagnostics
) {
    if (!replacement_allowed(policy, diagnostics)) return false;

    std::error_code error;
    const auto resolved_path = absolute_normalized(path, error);
    if (error) {
        return fail(diagnostics, "Could not resolve World document path: " + error.message());
    }

    const auto loaded = world::load_world_snapshot(resolved_path);
    if (!loaded.snapshot.has_value()) {
        return fail(
            diagnostics,
            "Could not open World document `" + resolved_path.generic_string() + "`: " +
                loaded.diagnostics
        );
    }

    auto replacement = std::make_unique<world::World>();
    if (!replacement->restore(*loaded.snapshot)) {
        return fail(
            diagnostics,
            "Could not restore World document `" + resolved_path.generic_string() + "`."
        );
    }

    world_ = std::move(replacement);
    path_ = resolved_path;
    dirty_ = false;
    if (diagnostics != nullptr) diagnostics->clear();
    return true;
}

bool WorldDocument::save(std::string* const diagnostics) {
    if (path_.empty()) return fail(diagnostics, "World document has no save path. Use Save As first.");
    return save_as(path_, diagnostics);
}

bool WorldDocument::save_as(
    const std::filesystem::path& path,
    std::string* const diagnostics
) {
    if (path.empty()) return fail(diagnostics, "World document save path is empty.");
    if (path.extension() != ".vworldc") {
        return fail(diagnostics, "World document save path must use the .vworldc extension.");
    }

    std::error_code error;
    const auto resolved_path = absolute_normalized(path, error);
    if (error) {
        return fail(diagnostics, "Could not resolve World document save path: " + error.message());
    }

    const auto parent = resolved_path.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, error);
        if (error) {
            return fail(diagnostics, "Could not create World document directory: " + error.message());
        }
    }

    std::string save_diagnostics;
    if (!world::save_world_snapshot(resolved_path, world_->snapshot(), &save_diagnostics)) {
        return fail(
            diagnostics,
            "Could not save World document `" + resolved_path.generic_string() + "`: " +
                save_diagnostics
        );
    }

    path_ = resolved_path;
    dirty_ = false;
    if (diagnostics != nullptr) diagnostics->clear();
    return true;
}

} // namespace vice::editor
