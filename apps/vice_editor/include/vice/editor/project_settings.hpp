#pragma once

#include <vice/editor/project_session.hpp>

#include <filesystem>
#include <string>

namespace vice::editor {

struct ProjectSettingsUpdate {
    std::string project_name;
    std::string game_name;
    std::filesystem::path startup_world;
};

[[nodiscard]] bool save_project_settings(
    ProjectSession& session,
    const ProjectSettingsUpdate& update,
    std::string* diagnostics = nullptr
);

} // namespace vice::editor
