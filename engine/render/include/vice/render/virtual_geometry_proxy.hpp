#pragma once

#include <vice/render/mesh_asset.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace vice::render {

inline constexpr std::uint32_t virtual_geometry_proxy_triangle_divisor = 2U;

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
