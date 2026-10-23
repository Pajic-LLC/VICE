#pragma once

#include <vice/render/mesh_asset.hpp>

#include <optional>
#include <span>
#include <string>

namespace vice::render {

struct MeshletBuildResult {
    std::optional<MeshletGeometry> geometry;
    std::string diagnostics;

    [[nodiscard]] explicit operator bool() const noexcept {
        return geometry.has_value();
    }
};

struct VirtualGeometryPageBuildResult {
    std::optional<VirtualGeometryPageTable> page_table;
    std::string diagnostics;

    [[nodiscard]] explicit operator bool() const noexcept {
        return page_table.has_value();
    }
};

struct VirtualGeometryHierarchyBuildResult {
    std::optional<VirtualGeometryHierarchy> hierarchy;
    std::string diagnostics;

    [[nodiscard]] explicit operator bool() const noexcept {
        return hierarchy.has_value();
    }
};

[[nodiscard]] MeshletBuildResult build_meshlets(
    std::span<const MeshVertex> vertices,
    std::span<const std::uint32_t> indices
);

[[nodiscard]] VirtualGeometryPageBuildResult build_virtual_geometry_pages(
    const MeshletGeometry& geometry
);

[[nodiscard]] VirtualGeometryHierarchyBuildResult build_virtual_geometry_hierarchy(
    const VirtualGeometryPageTable& page_table
);

} // namespace vice::render
