#include <vice/editor/project_workflow.hpp>

#include <vice/project/project.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <system_error>
#include <unordered_set>
#include <utility>
#include <vector>

namespace vice::editor {
namespace {

constexpr std::string_view recent_projects_header = "vice-recent-projects 1";

[[nodiscard]] std::optional<std::filesystem::path> normalized_existing_manifest(
    const std::filesystem::path& input
) {
    if (input.empty()) return std::nullopt;
    std::error_code error;
    auto path = std::filesystem::absolute(input, error).lexically_normal();
    if (error || !std::filesystem::is_regular_file(path, error) || error) {
        return std::nullopt;
    }
    return path;
}

[[nodiscard]] std::vector<std::filesystem::path> load_paths(
    const std::filesystem::path& state_file,
    const std::size_t limit
) {
    std::vector<std::filesystem::path> result;
    if (state_file.empty() || limit == 0U) return result;

    std::ifstream input{state_file, std::ios::binary};
    if (!input) return result;

    std::string header;
    if (!std::getline(input, header) || header != recent_projects_header) return result;

    std::unordered_set<std::string> seen;
    std::string encoded;
    while (result.size() < limit && input >> std::quoted(encoded)) {
        const auto path = normalized_existing_manifest(std::filesystem::path{encoded});
        if (!path.has_value()) continue;
        const std::string key = path->generic_string();
        if (!seen.insert(key).second) continue;
        result.push_back(*path);
    }
    return result;
}

[[nodiscard]] bool write_paths(
    const std::filesystem::path& state_file,
    const std::vector<std::filesystem::path>& paths,
    std::string* const diagnostics
) {
    if (state_file.empty()) {
        if (diagnostics != nullptr) *diagnostics = "Recent-project state path is empty.";
        return false;
    }

    std::error_code error;
    if (!state_file.parent_path().empty()) {
        std::filesystem::create_directories(state_file.parent_path(), error);
        if (error) {
            if (diagnostics != nullptr) {
                *diagnostics = "Could not create recent-project state directory: " + error.message();
            }
            return false;
        }
    }

    const auto temporary = state_file.string() + ".tmp";
    {
        std::ofstream output{temporary, std::ios::binary | std::ios::trunc};
        if (!output) {
            if (diagnostics != nullptr) *diagnostics = "Could not create recent-project state file.";
            return false;
        }
        output << recent_projects_header << '\n';
        for (const auto& path : paths) {
            output << std::quoted(path.generic_string()) << '\n';
        }
        if (!output) {
            if (diagnostics != nullptr) *diagnostics = "Could not write recent-project state file.";
            return false;
        }
    }

    std::filesystem::rename(temporary, state_file, error);
    if (error) {
        std::error_code remove_error;
        std::filesystem::remove(state_file, remove_error);
        error.clear();
        std::filesystem::rename(temporary, state_file, error);
    }
    if (error) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        if (diagnostics != nullptr) {
            *diagnostics = "Could not replace recent-project state file: " + error.message();
        }
        return false;
    }

    if (diagnostics != nullptr) diagnostics->clear();
    return true;
}

} // namespace

std::vector<RecentProject> load_recent_projects(
    const std::filesystem::path& state_file,
    const std::size_t limit
) {
    std::vector<RecentProject> result;
    for (const auto& path : load_paths(state_file, limit)) {
        const auto loaded = project::load_manifest(path);
        if (!loaded.manifest.has_value()) continue;
        result.push_back(RecentProject{
            .name = loaded.manifest->name,
            .manifest_path = path,
        });
    }
    return result;
}

bool remember_recent_project(
    const std::filesystem::path& state_file,
    const std::filesystem::path& manifest_path,
    std::string* const diagnostics,
    const std::size_t limit
) {
    if (limit == 0U) {
        if (diagnostics != nullptr) *diagnostics = "Recent-project limit must be greater than zero.";
        return false;
    }

    const auto normalized = normalized_existing_manifest(manifest_path);
    if (!normalized.has_value()) {
        if (diagnostics != nullptr) *diagnostics = "Recent project manifest does not exist.";
        return false;
    }
    const auto loaded = project::load_manifest(*normalized);
    if (!loaded.manifest.has_value()) {
        if (diagnostics != nullptr) *diagnostics = loaded.diagnostics;
        return false;
    }

    auto paths = load_paths(state_file, limit);
    paths.erase(
        std::remove(paths.begin(), paths.end(), *normalized),
        paths.end()
    );
    paths.insert(paths.begin(), *normalized);
    if (paths.size() > limit) paths.resize(limit);
    return write_paths(state_file, paths, diagnostics);
}

} // namespace vice::editor
