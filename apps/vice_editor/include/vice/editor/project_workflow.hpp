#pragma once

#include <vice/editor/project_session.hpp>

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace vice::editor {

struct ProjectCreateDesc {
    std::string name;
    std::filesystem::path parent_directory;
};

struct ProjectCreateResult {
    std::optional<std::filesystem::path> manifest_path;
    std::string diagnostics;

    [[nodiscard]] explicit operator bool() const noexcept {
        return manifest_path.has_value();
    }
};

struct RecentProject {
    std::string name;
    std::filesystem::path manifest_path;
};

[[nodiscard]] std::string project_folder_name(std::string_view display_name);
[[nodiscard]] ProjectCreateResult create_project(const ProjectCreateDesc& desc);

[[nodiscard]] bool cook_project_startup_world(
    const ProjectSession& session,
    std::string* diagnostics = nullptr
);

[[nodiscard]] std::vector<RecentProject> load_recent_projects(
    const std::filesystem::path& state_file,
    std::size_t limit = 8U
);

[[nodiscard]] bool remember_recent_project(
    const std::filesystem::path& state_file,
    const std::filesystem::path& manifest_path,
    std::string* diagnostics = nullptr,
    std::size_t limit = 8U
);

} // namespace vice::editor
