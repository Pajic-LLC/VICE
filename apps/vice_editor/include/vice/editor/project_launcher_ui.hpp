#pragma once

#include <vice/platform/event.hpp>
#include <vice/platform/types.hpp>

#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>

namespace vice::editor {

struct LauncherProjectEntry {
    std::string name;
    std::filesystem::path manifest_path;
};

struct NewProjectRequest {
    std::string name;
    std::filesystem::path parent_directory;
};

class ProjectLauncherUi final {
public:
    ProjectLauncherUi();
    ~ProjectLauncherUi();

    ProjectLauncherUi(const ProjectLauncherUi&) = delete;
    ProjectLauncherUi& operator=(const ProjectLauncherUi&) = delete;
    ProjectLauncherUi(ProjectLauncherUi&&) = delete;
    ProjectLauncherUi& operator=(ProjectLauncherUi&&) = delete;

    [[nodiscard]] bool valid() const noexcept;

    void handle_event(const platform::Event& event);
    [[nodiscard]] bool begin_frame(platform::WindowMetrics metrics, float delta_seconds);
    void draw(
        std::span<const LauncherProjectEntry> recent_projects,
        bool development_project_available
    );
    void end_frame();

    void set_new_project_parent(std::filesystem::path parent_directory);
    void set_error(std::string message);
    void clear_error();

    [[nodiscard]] bool consume_open_project_request() noexcept;
    [[nodiscard]] bool consume_choose_project_parent_request() noexcept;
    [[nodiscard]] bool consume_development_project_request() noexcept;
    [[nodiscard]] bool consume_quit_request() noexcept;
    [[nodiscard]] std::optional<NewProjectRequest> consume_new_project_request();
    [[nodiscard]] std::optional<std::filesystem::path> consume_recent_project_request();

private:
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace vice::editor
