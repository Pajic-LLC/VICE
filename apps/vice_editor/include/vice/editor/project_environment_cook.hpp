#pragma once

#include <vice/editor/project_session.hpp>

#include <filesystem>
#include <string>
#include <vector>

namespace vice::editor {

struct CookedProjectEnvironment {
    std::filesystem::path source;
    std::filesystem::path cooked;
};

struct ProjectEnvironmentCookResult {
    bool success = false;
    std::vector<CookedProjectEnvironment> environments;
    std::string diagnostics;

    [[nodiscard]] explicit operator bool() const noexcept { return success; }
};

[[nodiscard]] ProjectEnvironmentCookResult cook_project_environments(const ProjectSession& session);

} // namespace vice::editor
