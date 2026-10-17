#pragma once

#include <vice/editor/project_workflow.hpp>

#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace vice::editor {

struct ProjectWorkflowStatus {
    bool project_loaded = false;
    bool show_start_surface = false;
    std::string project_name;
    std::filesystem::path project_manifest;
    std::string world_name;
    bool world_dirty = false;
    bool world_has_path = false;
    std::filesystem::path world_path;
    std::filesystem::path development_project;
    std::string diagnostics;
};

class ProjectWorkflowUi final {
public:
    ProjectWorkflowUi();
    ~ProjectWorkflowUi();

    ProjectWorkflowUi(const ProjectWorkflowUi&) = delete;
    ProjectWorkflowUi& operator=(const ProjectWorkflowUi&) = delete;
    ProjectWorkflowUi(ProjectWorkflowUi&&) noexcept;
    ProjectWorkflowUi& operator=(ProjectWorkflowUi&&) noexcept;

    void draw(const ProjectWorkflowStatus& status);

    [[nodiscard]] std::optional<ProjectCreateRequest> consume_new_project_request();
    [[nodiscard]] std::optional<std::filesystem::path> consume_open_project_request();
    [[nodiscard]] bool consume_open_development_project_request() noexcept;
    [[nodiscard]] bool consume_new_world_request() noexcept;
    [[nodiscard]] std::optional<std::filesystem::path> consume_open_world_request();
    [[nodiscard]] bool consume_save_world_request() noexcept;
    [[nodiscard]] std::optional<std::filesystem::path> consume_save_world_as_request();

private:
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace vice::editor
