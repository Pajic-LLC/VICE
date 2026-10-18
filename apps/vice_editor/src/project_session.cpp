#include <vice/editor/project_session.hpp>
#include <vice/editor/project_workflow.hpp>

#include <vice/world/snapshot_file.hpp>

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <random>
#include <sstream>
#include <system_error>
#include <utility>

namespace vice::editor {
namespace {

constexpr std::size_t max_recent_projects = 8U;

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

[[nodiscard]] std::optional<RecentProjectEntry> recent_entry(
    const std::filesystem::path& manifest_path
) {
    std::error_code error;
    const auto resolved = absolute_normalized(manifest_path, error);
    if (error) return std::nullopt;
    const auto loaded = project::load_manifest(resolved);
    if (!loaded.manifest.has_value()) return std::nullopt;
    return RecentProjectEntry{
        .project_id = loaded.manifest->id,
        .name = loaded.manifest->name,
        .manifest_path = resolved,
    };
}

} // namespace

RecentProjects::RecentProjects(std::filesystem::path state_file)
    : state_file_{std::move(state_file)} {}

bool RecentProjects::load(std::string* const diagnostics) {
    entries_.clear();
    if (state_file_.empty()) {
        if (diagnostics != nullptr) diagnostics->clear();
        return true;
    }

    std::error_code error;
    const bool exists = std::filesystem::exists(state_file_, error);
    if (error) return fail(diagnostics, "Could not inspect recent-project state: " + error.message());
    if (!exists) {
        if (diagnostics != nullptr) diagnostics->clear();
        return true;
    }

    std::ifstream input{state_file_, std::ios::binary};
    if (!input) return fail(diagnostics, "Could not open recent-project state.");

    std::string line;
    while (entries_.size() < max_recent_projects && std::getline(input, line)) {
        std::istringstream stream{line};
        std::string path_text;
        if (!(stream >> std::quoted(path_text))) continue;
        stream >> std::ws;
        if (!stream.eof()) continue;
        const auto entry = recent_entry(std::filesystem::path{path_text});
        if (!entry.has_value()) continue;
        const bool duplicate = std::any_of(
            entries_.begin(), entries_.end(),
            [&](const RecentProjectEntry& existing) {
                return existing.project_id == entry->project_id ||
                    existing.manifest_path == entry->manifest_path;
            }
        );
        if (!duplicate) entries_.push_back(*entry);
    }
    if (!input.eof() && input.fail()) {
        return fail(diagnostics, "Could not read recent-project state.");
    }
    if (diagnostics != nullptr) diagnostics->clear();
    return true;
}

bool RecentProjects::persist(std::string* const diagnostics) {
    if (state_file_.empty()) return fail(diagnostics, "Recent-project state path is empty.");
    std::error_code error;
    const auto parent = state_file_.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, error);
        if (error) return fail(diagnostics, "Could not create recent-project state directory: " + error.message());
    }

    std::ofstream output{state_file_, std::ios::binary | std::ios::trunc};
    if (!output) return fail(diagnostics, "Could not write recent-project state.");
    for (const auto& entry : entries_) {
        output << std::quoted(entry.manifest_path.generic_string()) << '\n';
    }
    if (!output) return fail(diagnostics, "Could not finish writing recent-project state.");
    if (diagnostics != nullptr) diagnostics->clear();
    return true;
}

bool RecentProjects::remember(
    const std::filesystem::path& manifest_path,
    std::string* const diagnostics
) {
    const auto entry = recent_entry(manifest_path);
    if (!entry.has_value()) return fail(diagnostics, "Recent project is not a valid VICE project manifest.");

    std::erase_if(entries_, [&](const RecentProjectEntry& existing) {
        return existing.project_id == entry->project_id || existing.manifest_path == entry->manifest_path;
    });
    entries_.insert(entries_.begin(), *entry);
    if (entries_.size() > max_recent_projects) entries_.resize(max_recent_projects);
    return persist(diagnostics);
}

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
    const auto game = staging / "game";
    std::filesystem::create_directories(worlds, error);
    if (!error) {
        std::filesystem::create_directories(game, error);
    }
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
        "region 1 1 0 0 0 0 0 0 full bounds -100 -100 -100 100 100 100\n"
        "entity a0000000-0000-4000-8000-000000000001 spatial 0 3 8 1 camera perspective 60 0.05 1000 primary\n"
        "entity a0000000-0000-4000-8000-000000000002 spatial 0 6 4 1 transform -0.25881905 0 0 0.96592583 1 1 1 light directional 1 0.95 0.9 3 enabled\n";

    const std::string game_text =
        "#include <vice/runtime/module_api.hpp>\n\n"
        "namespace {\n\n"
        "using vice::runtime::module::Context;\n"
        "using vice::runtime::module::Frame;\n"
        "using vice::runtime::module::LogLevel;\n\n"
        "class Game final : public vice::runtime::module::Game {\n"
        "public:\n"
        "    bool on_start(Context& context) override {\n"
        "        context.log(LogLevel::info, \"Game started.\");\n"
        "        return true;\n"
        "    }\n\n"
        "    void on_frame(Context&, Frame& frame) override {\n"
        "        frame.clear_color = {0.04F, 0.06F, 0.09F, 1.0F};\n"
        "    }\n"
        "};\n\n"
        "} // namespace\n\n"
        "VICE_GAME_MODULE(Game)\n";

    std::string diagnostics;
    if (!write_text(staging / "vice.viceproj", manifest_text, diagnostics) ||
        !write_text(worlds / "main.vworld", world_text, diagnostics) ||
        !write_text(game / "game.cpp", game_text, diagnostics)) {
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
    session.cooked_root_ = project::resolve_cooked_root(cooked_base, session.manifest_);
    session.cooked_startup_world_ = project::resolve_cooked_startup_world(cooked_base, session.manifest_);
    if (session.asset_root_.empty() || session.startup_world_source_.empty() ||
        session.cooked_root_.empty() || session.cooked_startup_world_.empty()) {
        if (diagnostics != nullptr) *diagnostics = "Project asset or runtime paths could not be resolved.";
        return std::nullopt;
    }

    const std::string project_key = session.manifest_.id.to_string();
    session.editor_state_root_ = (state_base / project_key).lexically_normal();
    session.layout_path_ = session.editor_state_root_ / "layout.ini";
    session.working_world_path_ = session.editor_state_root_ / "world.vworldc";

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
    *world_ = world::World{};
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

    world::World replacement;
    if (!replacement.restore(*loaded.snapshot)) {
        return fail(
            diagnostics,
            "Could not restore World document `" + resolved_path.generic_string() + "`."
        );
    }

    *world_ = std::move(replacement);
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