#include <vice/editor/project_session.hpp>

#include <vice/world/snapshot_file.hpp>

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

} // namespace

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
