#include <vice/editor/project_workflow.hpp>

#include <vice/project/project.hpp>
#include <vice/tools/world_cooker.hpp>
#include <vice/world/snapshot_file.hpp>

#include <array>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <system_error>

namespace vice::editor {
namespace {

constexpr std::string_view default_world_source =
    "vice-world-source 1\n"
    "\n"
    "region 1 1 0 0 0 0 0 0 full bounds -256 -128 -256 256 128 256\n";

[[nodiscard]] bool blank(const std::string_view text) noexcept {
    for (const unsigned char value : text) {
        if (!std::isspace(value)) return false;
    }
    return true;
}

[[nodiscard]] project::ProjectId generate_project_id() {
    project::ProjectId id;
    std::random_device random;
    for (auto& byte : id.bytes) {
        byte = static_cast<std::uint8_t>(random() & 0xffU);
    }
    id.bytes[6] = static_cast<std::uint8_t>((id.bytes[6] & 0x0fU) | 0x40U);
    id.bytes[8] = static_cast<std::uint8_t>((id.bytes[8] & 0x3fU) | 0x80U);
    return id;
}

[[nodiscard]] bool write_text_file(
    const std::filesystem::path& path,
    const std::string_view text,
    std::string& diagnostics
) {
    std::ofstream output{path, std::ios::binary | std::ios::trunc};
    if (!output) {
        diagnostics = "Could not create `" + path.generic_string() + "`.";
        return false;
    }
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!output) {
        diagnostics = "Could not write `" + path.generic_string() + "`.";
        return false;
    }
    return true;
}

[[nodiscard]] std::optional<std::string> read_text_file(
    const std::filesystem::path& path,
    std::string& diagnostics
) {
    std::ifstream input{path, std::ios::binary};
    if (!input) {
        diagnostics = "Could not open `" + path.generic_string() + "`.";
        return std::nullopt;
    }
    std::ostringstream stream;
    stream << input.rdbuf();
    if (!input.good() && !input.eof()) {
        diagnostics = "Could not read `" + path.generic_string() + "`.";
        return std::nullopt;
    }
    return stream.str();
}

class ProjectRollback final {
public:
    explicit ProjectRollback(std::filesystem::path root) : root_{std::move(root)} {}
    ~ProjectRollback() {
        if (committed_) return;
        std::error_code ignored;
        std::filesystem::remove_all(root_, ignored);
    }

    void commit() noexcept { committed_ = true; }

private:
    std::filesystem::path root_;
    bool committed_ = false;
};

} // namespace

std::string project_folder_name(const std::string_view display_name) {
    std::string result;
    result.reserve(display_name.size());
    bool previous_separator = false;

    for (const unsigned char value : display_name) {
        if (value < 0x20U) continue;
        const bool invalid = value == '<' || value == '>' || value == ':' || value == '"' ||
            value == '/' || value == '\\' || value == '|' || value == '?' || value == '*';
        if (invalid) {
            if (!result.empty() && !previous_separator) {
                result.push_back('-');
                previous_separator = true;
            }
            continue;
        }
        if (std::isspace(value)) {
            if (!result.empty() && !previous_separator) {
                result.push_back(' ');
                previous_separator = true;
            }
            continue;
        }
        result.push_back(static_cast<char>(value));
        previous_separator = false;
    }

    while (!result.empty() && (result.front() == ' ' || result.front() == '.')) result.erase(result.begin());
    while (!result.empty() && (result.back() == ' ' || result.back() == '.')) result.pop_back();
    return result.empty() ? std::string{"VICE Project"} : result;
}

ProjectCreateResult create_project(const ProjectCreateDesc& desc) {
    ProjectCreateResult result;
    if (desc.name.empty() || blank(desc.name)) {
        result.diagnostics = "Project name cannot be empty.";
        return result;
    }
    if (desc.parent_directory.empty()) {
        result.diagnostics = "Project parent folder is not set.";
        return result;
    }

    std::error_code error;
    const auto parent = std::filesystem::absolute(desc.parent_directory, error).lexically_normal();
    if (error || !std::filesystem::is_directory(parent, error) || error) {
        result.diagnostics = error
            ? "Could not inspect the project parent folder: " + error.message()
            : "Project parent folder does not exist.";
        return result;
    }

    const auto project_root = (parent / project_folder_name(desc.name)).lexically_normal();
    const bool exists = std::filesystem::exists(project_root, error);
    if (error) {
        result.diagnostics = "Could not inspect the project destination: " + error.message();
        return result;
    }
    if (exists) {
        result.diagnostics = "Project destination already exists: " + project_root.generic_string();
        return result;
    }

    if (!std::filesystem::create_directories(project_root / "assets" / "worlds", error) || error) {
        result.diagnostics = "Could not create the project folder: " + error.message();
        return result;
    }
    ProjectRollback rollback{project_root};

    for (const auto* child : {"meshes", "collision", "materials", "shaders"}) {
        std::filesystem::create_directories(project_root / "assets" / child, error);
        if (error) {
            result.diagnostics = "Could not create the project asset folders: " + error.message();
            return result;
        }
    }

    project::Manifest manifest;
    manifest.id = generate_project_id();
    manifest.name = desc.name;
    manifest.asset_root = "assets";
    manifest.startup_world = std::filesystem::path{"worlds"} / "main.vworld";
    if (!project::validate(manifest, &result.diagnostics)) return result;

    const auto manifest_path = project_root / "vice.viceproj";
    if (!write_text_file(manifest_path, project::serialize_manifest(manifest), result.diagnostics)) {
        return result;
    }
    if (!write_text_file(
            project_root / "assets" / "worlds" / "main.vworld",
            default_world_source,
            result.diagnostics
        )) {
        return result;
    }

    rollback.commit();
    result.manifest_path = manifest_path;
    return result;
}

bool cook_project_startup_world(
    const ProjectSession& session,
    std::string* const diagnostics
) {
    std::string local_diagnostics;
    const auto source = read_text_file(session.startup_world_source(), local_diagnostics);
    if (!source.has_value()) {
        if (diagnostics != nullptr) *diagnostics = std::move(local_diagnostics);
        return false;
    }

    const auto parsed = tools::world_cooker::parse_source_manifest(*source);
    if (!parsed.snapshot.has_value()) {
        if (diagnostics != nullptr) {
            *diagnostics = "Could not cook startup World `" +
                session.startup_world_source().generic_string() + "`: " + parsed.diagnostics;
        }
        return false;
    }

    std::error_code error;
    std::filesystem::create_directories(session.cooked_startup_world().parent_path(), error);
    if (error) {
        if (diagnostics != nullptr) {
            *diagnostics = "Could not create the cooked World cache folder: " + error.message();
        }
        return false;
    }

    std::string save_diagnostics;
    if (!world::save_world_snapshot(
            session.cooked_startup_world(),
            *parsed.snapshot,
            &save_diagnostics
        )) {
        if (diagnostics != nullptr) {
            *diagnostics = "Could not write the cooked startup World: " + save_diagnostics;
        }
        return false;
    }

    if (diagnostics != nullptr) diagnostics->clear();
    return true;
}

} // namespace vice::editor
