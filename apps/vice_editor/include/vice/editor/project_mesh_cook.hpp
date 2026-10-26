#pragma once

#include <vice/editor/project_session.hpp>
#include <vice/tools/project_mesh_cook.hpp>

namespace vice::editor {

using CookedProjectMesh = tools::CookedProjectMesh;
using ProjectMeshCookResult = tools::ProjectMeshCookResult;

[[nodiscard]] ProjectMeshCookResult cook_project_meshes(const ProjectSession& session);

} // namespace vice::editor
