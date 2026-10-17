#include <vice/editor/project_workflow.hpp>

#include <fstream>
#include <random>
#include <system_error>

namespace vice::editor {
namespace {

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

} // namespace vice::editor
