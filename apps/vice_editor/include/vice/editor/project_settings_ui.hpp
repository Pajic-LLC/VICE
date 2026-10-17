#pragma once

#include <vice/editor/asset_index.hpp>
#include <vice/editor/project_settings.hpp>
#include <vice/project/project.hpp>

#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace vice::editor {

struct ProjectSettingsStatus {
    project::ProjectId project_id{};
    std::string project_name;
    std::string game_name;
    std::filesystem::path asset_root;
    std::filesystem::path startup_world;
};

class ProjectSettingsUi final {
public:
    ProjectSettingsUi();
    ~ProjectSettingsUi();

    ProjectSettingsUi(const ProjectSettingsUi&) = delete;
    ProjectSettingsUi& operator=(const ProjectSettingsUi&) = delete;
    ProjectSettingsUi(ProjectSettingsUi&&) noexcept;
    ProjectSettingsUi& operator=(ProjectSettingsUi&&) noexcept;

    void draw(const ProjectSettingsStatus& status, const AssetIndex& assets);

    [[nodiscard]] std::optional<ProjectSettingsUpdate> consume_save_request();
    void complete_save(bool success, std::string diagnostics = {});

private:
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace vice::editor
