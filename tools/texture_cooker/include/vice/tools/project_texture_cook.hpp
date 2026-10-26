#pragma once

#include <vice/project/workspace.hpp>
#include <vice/world/resource.hpp>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace vice::tools {

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

[[nodiscard]] ProjectTextureCookResult cook_project_textures(const project::Workspace& workspace);

[[nodiscard]] std::vector<world::ResourceId> cooked_texture_ids(
    const ProjectTextureCookResult& result
);

} // namespace vice::tools
