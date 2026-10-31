#pragma once
#include <vice/presentation/assets.hpp>
#include <vice/project/workspace.hpp>
namespace vice::tools {
// Bounded authoring inputs for dense multi-part characters. Skin evaluation
// retains its independent per-skin limit; these do not change archive formats.
inline constexpr std::size_t rig_import_source_bytes = 64ULL * 1024 * 1024;
inline constexpr std::size_t rig_import_parser_bytes = 192ULL * 1024 * 1024;
inline constexpr std::size_t rig_import_mesh_vertices = 200000;
inline constexpr std::size_t rig_import_total_vertices = 1000000;
inline constexpr std::size_t rig_import_mesh_indices = 1200000;
inline constexpr std::size_t rig_import_total_indices = 6000000;
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
