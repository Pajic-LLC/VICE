#pragma once

#include <vice/editor/project_session.hpp>
#include <vice/tools/project_environment_cook.hpp>

namespace vice::editor {

using CookedProjectEnvironment = tools::CookedProjectEnvironment;
using ProjectEnvironmentCookResult = tools::ProjectEnvironmentCookResult;

[[nodiscard]] ProjectEnvironmentCookResult cook_project_environments(const ProjectSession& session);

} // namespace vice::editor
