#pragma once

#include <vice/editor/project_session.hpp>
#include <vice/world/resource.hpp>

#include <cstddef>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace vice::editor {

struct ProjectTextureCookRecord {
    std::filesystem::path source;
    std::filesystem::path cooked;
    world::ResourceId resource_id{};
    std::uint32_t width = 0U;
    std::uint32_t height = 0U;
};

struct ProjectTextureCookResult {
    bool success = false;
    std::vector<ProjectTextureCookRecord> textures;
    std::string diagnostics;

    [[nodiscard]] explicit operator bool() const noexcept { return success; }
};

[[nodiscard]] ProjectTextureCookResult cook_project_textures(const ProjectSession& session);

[[nodiscard]] std::vector<world::ResourceId> cooked_texture_ids(
    const ProjectTextureCookResult& result
);

} // namespace vice::editor
