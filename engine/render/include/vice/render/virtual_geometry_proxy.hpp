#pragma once

#include <vice/render/mesh_asset.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace vice::render {

inline constexpr std::uint32_t virtual_geometry_proxy_triangle_divisor = 2U;

struct VirtualGeometryProxyMesh {
    std::uint32_t node_index = virtual_geometry_invalid_node;
    std::uint32_t source_triangle_count = 0U;
    float direct_geometric_error = 0.0F;
    std::vector<MeshVertex> vertices;
    std::vector<std::uint32_t> indices;
};

struct VirtualGeometryProxySet {
    VirtualGeometryHierarchy hierarchy;
    std::vector<VirtualGeometryProxyMesh> proxies;
};

struct VirtualGeometryProxyBuildResult {
    std::optional<VirtualGeometryProxySet> proxy_set;
    std::string diagnostics;

    [[nodiscard]] explicit operator bool() const noexcept {
        return proxy_set.has_value();
    }
};

[[nodiscard]] VirtualGeometryProxyBuildResult build_virtual_geometry_proxies(
    std::span<const MeshVertex> vertices,
    const MeshletGeometry& geometry,
    const VirtualGeometryPageTable& page_table,
    const VirtualGeometryHierarchy& hierarchy
);

} // namespace vice::render
