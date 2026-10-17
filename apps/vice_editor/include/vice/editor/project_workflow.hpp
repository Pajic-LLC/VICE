#pragma once

#include <vice/project/project.hpp>

#include <filesystem>
#include <optional>
#include <string>

namespace vice::editor {

struct ProjectCreateRequest {
    std::string name;
    std::filesystem::path parent_directory;
    std::optional<project::ProjectId> project_id;
};

struct ProjectCreateResult {
    std::filesystem::path manifest_path;
    project::ProjectId project_id{};
    std::string diagnostics;

    [[nodiscard]] explicit operator bool() const noexcept {
        return !manifest_path.empty() && static_cast<bool>(project_id) && diagnostics.empty();
    }
};

[[nodiscard]] project::ProjectId generate_project_id();
[[nodiscard]] ProjectCreateResult create_project(const ProjectCreateRequest& request);

} // namespace vice::editor
