#pragma once

#include <vice/editor/project_session.hpp>
#include <vice/tools/project_texture_cook.hpp>

namespace vice::editor {

using ProjectTextureCookRecord = tools::ProjectTextureCookRecord;
using ProjectTextureCookResult = tools::ProjectTextureCookResult;

[[nodiscard]] ProjectTextureCookResult cook_project_textures(const ProjectSession& session);

} // namespace vice::editor
