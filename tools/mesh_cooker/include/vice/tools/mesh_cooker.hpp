#pragma once

#include <vice/render/mesh_asset.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace vice::tools::mesh_cooker {

inline constexpr std::uint32_t mesh_source_format_version_v1 = 1U;
inline constexpr std::uint32_t mesh_source_format_version_v2 = 2U;
inline constexpr std::uint32_t mesh_source_format_version = mesh_source_format_version_v2;

enum class CollisionMode : std::uint8_t {
    none,
    triangle_mesh,
    convex_hull,
};

struct MeshSource {
    std::uint32_t version = mesh_source_format_version;
    std::string name;
    render::MeshStableId stable_id{};
    CollisionMode collision = CollisionMode::none;
    std::vector<render::MeshVertex> vertices;
    std::vector<std::uint32_t> indices;
};

struct ParseResult {
    std::optional<MeshSource> source;
    std::string diagnostics;
};

struct CookResult {
    bool success = false;
    std::uint64_t source_fingerprint = 0U;
    std::vector<std::byte> bytes;
    std::string diagnostics;
};

[[nodiscard]] ParseResult parse_source_manifest(std::string_view source);
[[nodiscard]] CookResult cook(const MeshSource& source);
[[nodiscard]] bool write_asset(
    const std::filesystem::path& path,
    std::span<const std::byte> bytes,
    std::string* diagnostics = nullptr
);

} // namespace vice::tools::mesh_cooker
