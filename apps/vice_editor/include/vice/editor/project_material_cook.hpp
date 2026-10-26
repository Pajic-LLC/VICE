#pragma once

#include <vice/editor/project_session.hpp>
#include <vice/tools/project_render_material_cook.hpp>

#include <span>

namespace vice::editor {

using CookedProjectRenderMaterial = tools::ProjectRenderMaterialCookRecord;
using ProjectMaterialCookResult = tools::ProjectRenderMaterialCookResult;

[[nodiscard]] ProjectMaterialCookResult cook_project_render_materials(
    const ProjectSession& session,
    std::span<const world::ResourceId> available_textures = {}
);

} // namespace vice::editor
