#pragma once

#include <filesystem>
#include <optional>

namespace vice::editor::detail {

[[nodiscard]] std::optional<std::filesystem::path> run_project_launcher(
    const std::filesystem::path& development_project,
    const std::filesystem::path& cooked_cache_root,
    const std::filesystem::path& editor_state_root,
    const std::filesystem::path& recent_projects_file
);

} // namespace vice::editor::detail
