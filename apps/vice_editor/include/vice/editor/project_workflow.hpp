#pragma once

#include <vice/editor/project_session.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

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

[[nodiscard]] std::string project_folder_name(std::string_view display_name);
[[nodiscard]] ProjectCreateResult create_project(const ProjectCreateDesc& desc);

[[nodiscard]] bool cook_project_startup_world(
    const ProjectSession& session,
    std::string* diagnostics = nullptr
);

} // namespace vice::editor
