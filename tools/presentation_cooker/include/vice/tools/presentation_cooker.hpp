#pragma once
#include <vice/presentation/assets.hpp>
#include <vice/project/workspace.hpp>
namespace vice::tools {
struct PresentationCookResult {
    bool success = false;
    std::size_t rigs = 0, sounds = 0;
    std::string diagnostics;
    explicit operator bool() const { return success; }
};
[[nodiscard]] PresentationCookResult cook_project_presentation(const project::Workspace &workspace);
[[nodiscard]] std::optional<presentation::Sound> import_wav(const std::filesystem::path &path,
                                                            world::ResourceId id,
                                                            std::string *diagnostics = nullptr);
[[nodiscard]] bool import_rig(const std::filesystem::path &gltf,
                              const std::filesystem::path &output, world::ResourceId id,
                              world::PersistentId root, std::string *diagnostics = nullptr);
} // namespace vice::tools
