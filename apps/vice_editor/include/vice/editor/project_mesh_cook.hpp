#pragma once

#include <vice/dev/project_mesh_cook.hpp>
#include <vice/editor/project_session.hpp>

namespace vice::editor {

using CookedProjectMesh = dev::CookedProjectMesh;
using ProjectMeshCookResult = dev::ProjectMeshCookResult;

[[nodiscard]] ProjectMeshCookResult cook_project_meshes(const ProjectSession& session);

} // namespace vice::editor
