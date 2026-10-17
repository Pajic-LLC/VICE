#pragma once

#include <vice/project/project.hpp>

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

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

struct RecentProjectEntry {
    project::ProjectId project_id{};
    std::string name;
    std::filesystem::path manifest_path;
};

class RecentProjects final {
public:
    explicit RecentProjects(std::filesystem::path state_file = {});

    bool load(std::string* diagnostics = nullptr);
    bool remember(const std::filesystem::path& manifest_path, std::string* diagnostics = nullptr);

    [[nodiscard]] std::span<const RecentProjectEntry> entries() const noexcept {
        return entries_;
    }

private:
    bool persist(std::string* diagnostics);

    std::filesystem::path state_file_;
    std::vector<RecentProjectEntry> entries_;
};

[[nodiscard]] project::ProjectId generate_project_id();
[[nodiscard]] ProjectCreateResult create_project(const ProjectCreateRequest& request);

} // namespace vice::editor
