#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace vice::render {

inline constexpr std::uint32_t mesh_asset_format_version_v1 = 1U;
inline constexpr std::uint32_t mesh_asset_format_version_v2 = 2U;
inline constexpr std::uint32_t mesh_asset_format_version_v3 = 3U;
inline constexpr std::uint32_t mesh_asset_format_version_v4 = 4U;
inline constexpr std::uint32_t mesh_asset_format_version_v5 = 5U;
inline constexpr std::uint32_t mesh_asset_format_version = mesh_asset_format_version_v5;
inline constexpr std::uint32_t mesh_asset_min_supported_version = mesh_asset_format_version_v1;
inline constexpr std::size_t mesh_asset_magic_size = 8U;
inline constexpr std::size_t mesh_stable_id_size = 16U;
inline constexpr std::uint32_t meshlet_max_vertices = 64U;
inline constexpr std::uint32_t meshlet_max_triangles = 126U;
inline constexpr std::uint32_t virtual_geometry_page_max_meshlets = 32U;
inline constexpr std::uint32_t virtual_geometry_hierarchy_fanout = 4U;
inline constexpr std::uint32_t virtual_geometry_invalid_node =
    std::numeric_limits<std::uint32_t>::max();

using MeshStableId = std::array<std::uint8_t, mesh_stable_id_size>;

[[nodiscard]] constexpr bool valid(const MeshStableId& id) noexcept {
    for (const std::uint8_t value : id) {
        if (value != 0U) return true;
    }
    return false;
}

struct MeshVertex {
    float position[3]{};
    float color[3]{1.0F, 1.0F, 1.0F};
    float normal[3]{0.0F, 1.0F, 0.0F};
    float uv[2]{};
};

static_assert(sizeof(MeshVertex) == 44U);

struct MeshletBounds {
    std::array<float, 3U> center{};
    float radius = 0.0F;

    friend bool operator==(const MeshletBounds&, const MeshletBounds&) = default;
};

struct Meshlet {
    std::uint32_t vertex_offset = 0U;
    std::uint32_t vertex_count = 0U;
    std::uint32_t triangle_offset = 0U;
    std::uint32_t triangle_count = 0U;
    MeshletBounds bounds{};

    friend bool operator==(const Meshlet&, const Meshlet&) = default;
};

struct MeshletGeometry {
    std::vector<Meshlet> meshlets;
    std::vector<std::uint32_t> vertex_indices;
    std::vector<std::uint8_t> triangle_indices;

    friend bool operator==(const MeshletGeometry&, const MeshletGeometry&) = default;
};

struct VirtualGeometryPage {
    std::uint32_t meshlet_offset = 0U;
    std::uint32_t meshlet_count = 0U;
    std::uint32_t vertex_index_offset = 0U;
    std::uint32_t vertex_index_count = 0U;
    std::uint32_t triangle_index_offset = 0U;
    std::uint32_t triangle_index_count = 0U;
    MeshletBounds bounds{};

    friend bool operator==(const VirtualGeometryPage&, const VirtualGeometryPage&) = default;
};

struct VirtualGeometryPageTable {
    std::vector<VirtualGeometryPage> pages;

    friend bool operator==(const VirtualGeometryPageTable&, const VirtualGeometryPageTable&) = default;
};

struct VirtualGeometryHierarchyNode {
    std::uint32_t parent_index = virtual_geometry_invalid_node;
    std::uint32_t first_child = virtual_geometry_invalid_node;
    std::uint32_t child_count = 0U;
    std::uint32_t page_offset = 0U;
    std::uint32_t page_count = 0U;
    std::uint32_t depth = 0U;
    MeshletBounds bounds{};
    float geometric_error = 0.0F;

    friend bool operator==(const VirtualGeometryHierarchyNode&, const VirtualGeometryHierarchyNode&) = default;
};

struct VirtualGeometryHierarchy {
    std::vector<VirtualGeometryHierarchyNode> nodes;
    std::uint32_t root_index = virtual_geometry_invalid_node;

    friend bool operator==(const VirtualGeometryHierarchy&, const VirtualGeometryHierarchy&) = default;
};

struct MeshAsset {
    std::uint32_t version = mesh_asset_format_version;
    MeshStableId stable_id{};
    std::uint64_t source_fingerprint = 0U;
    std::vector<MeshVertex> vertices;
    std::vector<std::uint32_t> indices;
    MeshletGeometry meshlet_geometry;
    VirtualGeometryPageTable virtual_geometry_pages;
};

struct MeshAssetDecodeResult {
    std::optional<MeshAsset> asset;
    std::string diagnostics;
};

[[nodiscard]] MeshAssetDecodeResult decode_mesh_asset(std::span<const std::byte> bytes);
[[nodiscard]] MeshAssetDecodeResult load_mesh_asset(const std::filesystem::path& path);

} // namespace vice::render
