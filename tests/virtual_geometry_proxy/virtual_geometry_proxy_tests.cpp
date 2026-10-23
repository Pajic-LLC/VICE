#include <vice/render/meshlet.hpp>
#include <vice/render/virtual_geometry_proxy.hpp>

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace {

struct ProxyInput {
    std::vector<vice::render::MeshVertex> vertices;
    vice::render::MeshletGeometry geometry;
    vice::render::VirtualGeometryPageTable pages;
    vice::render::VirtualGeometryHierarchy hierarchy;
};

[[nodiscard]] bool same_vertex(
    const vice::render::MeshVertex& lhs,
    const vice::render::MeshVertex& rhs
) noexcept {
    for (std::size_t axis = 0U; axis < 3U; ++axis) {
        if (lhs.position[axis] != rhs.position[axis] || lhs.color[axis] != rhs.color[axis] ||
            lhs.normal[axis] != rhs.normal[axis]) {
            return false;
        }
    }
    return lhs.uv[0] == rhs.uv[0] && lhs.uv[1] == rhs.uv[1];
}

[[nodiscard]] vice::render::MeshletBounds triangle_bounds(
    const std::vector<vice::render::MeshVertex>& vertices,
    const std::array<std::uint32_t, 3U>& triangle
) {
    vice::render::MeshletBounds bounds;
    for (const auto index : triangle) {
        for (std::size_t axis = 0U; axis < 3U; ++axis) {
            bounds.center[axis] += vertices[index].position[axis] / 3.0F;
        }
    }
    double radius_squared = 0.0;
    for (const auto index : triangle) {
        double distance_squared = 0.0;
        for (std::size_t axis = 0U; axis < 3U; ++axis) {
            const double delta = static_cast<double>(vertices[index].position[axis]) -
                bounds.center[axis];
            distance_squared += delta * delta;
        }
        radius_squared = std::max(radius_squared, distance_squared);
    }
    bounds.radius = static_cast<float>(std::sqrt(radius_squared));
    return bounds;
}

void append_triangle_meshlet(
    ProxyInput& input,
    const std::array<std::uint32_t, 3U>& triangle
) {
    const auto vertex_offset = static_cast<std::uint32_t>(input.geometry.vertex_indices.size());
    const auto triangle_offset = static_cast<std::uint32_t>(input.geometry.triangle_indices.size());
    input.geometry.meshlets.push_back(vice::render::Meshlet{
        .vertex_offset = vertex_offset,
        .vertex_count = 3U,
        .triangle_offset = triangle_offset,
        .triangle_count = 1U,
        .bounds = triangle_bounds(input.vertices, triangle),
    });
    input.geometry.vertex_indices.insert(
        input.geometry.vertex_indices.end(), triangle.begin(), triangle.end()
    );
    input.geometry.triangle_indices.push_back(0U);
    input.geometry.triangle_indices.push_back(1U);
    input.geometry.triangle_indices.push_back(2U);
}

void finalize(ProxyInput& input) {
    const auto page_build = vice::render::build_virtual_geometry_pages(input.geometry);
    assert(page_build);
    input.pages = *page_build.page_table;
    const auto hierarchy_build = vice::render::build_virtual_geometry_hierarchy(input.pages);
    assert(hierarchy_build);
    input.hierarchy = *hierarchy_build.hierarchy;
}

[[nodiscard]] ProxyInput make_grid(const std::uint32_t cells_x, const std::uint32_t cells_z) {
    ProxyInput input;
    const std::uint32_t vertices_x = cells_x + 1U;
    const std::uint32_t vertices_z = cells_z + 1U;
    input.vertices.reserve(static_cast<std::size_t>(vertices_x) * vertices_z);
    for (std::uint32_t z = 0U; z < vertices_z; ++z) {
        for (std::uint32_t x = 0U; x < vertices_x; ++x) {
            vice::render::MeshVertex vertex;
            vertex.position[0] = static_cast<float>(x);
            vertex.position[1] = 0.0F;
            vertex.position[2] = static_cast<float>(z);
            vertex.color[0] = static_cast<float>(x) / static_cast<float>(vertices_x);
            vertex.color[1] = static_cast<float>(z) / static_cast<float>(vertices_z);
            vertex.color[2] = 0.5F;
            vertex.normal[0] = 0.0F;
            vertex.normal[1] = 1.0F;
            vertex.normal[2] = 0.0F;
            vertex.uv[0] = static_cast<float>(x) / static_cast<float>(cells_x);
            vertex.uv[1] = static_cast<float>(z) / static_cast<float>(cells_z);
            input.vertices.push_back(vertex);
        }
    }

    const auto index = [vertices_x](const std::uint32_t x, const std::uint32_t z) {
        return z * vertices_x + x;
    };
    for (std::uint32_t z = 0U; z < cells_z; ++z) {
        for (std::uint32_t x = 0U; x < cells_x; ++x) {
            const auto a = index(x, z);
            const auto b = index(x + 1U, z);
            const auto c = index(x, z + 1U);
            const auto d = index(x + 1U, z + 1U);
            append_triangle_meshlet(input, {a, c, b});
            append_triangle_meshlet(input, {b, c, d});
        }
    }
    finalize(input);
    return input;
}

[[nodiscard]] ProxyInput make_triangle_islands(const std::uint32_t triangle_count) {
    ProxyInput input;
    input.vertices.reserve(static_cast<std::size_t>(triangle_count) * 3U);
    for (std::uint32_t triangle = 0U; triangle < triangle_count; ++triangle) {
        const float base_x = static_cast<float>(triangle) * 3.0F;
        const auto base_index = static_cast<std::uint32_t>(input.vertices.size());
        for (std::uint32_t corner = 0U; corner < 3U; ++corner) {
            vice::render::MeshVertex vertex;
            vertex.position[0] = base_x + (corner == 1U ? 1.0F : 0.0F);
            vertex.position[1] = 0.0F;
            vertex.position[2] = corner == 2U ? 1.0F : 0.0F;
            vertex.color[0] = 0.25F;
            vertex.color[1] = 0.5F;
            vertex.color[2] = 0.75F;
            vertex.normal[1] = 1.0F;
            input.vertices.push_back(vertex);
        }
        append_triangle_meshlet(input, {base_index, base_index + 2U, base_index + 1U});
    }
    finalize(input);
    return input;
}

[[nodiscard]] bool same_proxy_mesh(
    const vice::render::VirtualGeometryProxyMesh& lhs,
    const vice::render::VirtualGeometryProxyMesh& rhs
) {
    if (lhs.node_index != rhs.node_index ||
        lhs.source_triangle_count != rhs.source_triangle_count ||
        lhs.direct_geometric_error != rhs.direct_geometric_error ||
        lhs.indices != rhs.indices || lhs.vertices.size() != rhs.vertices.size()) {
        return false;
    }
    for (std::size_t index = 0U; index < lhs.vertices.size(); ++index) {
        if (!same_vertex(lhs.vertices[index], rhs.vertices[index])) return false;
    }
    return true;
}

[[nodiscard]] bool same_proxy_set(
    const vice::render::VirtualGeometryProxySet& lhs,
    const vice::render::VirtualGeometryProxySet& rhs
) {
    if (!(lhs.hierarchy == rhs.hierarchy) || lhs.proxies.size() != rhs.proxies.size()) {
        return false;
    }
    for (std::size_t index = 0U; index < lhs.proxies.size(); ++index) {
        if (!same_proxy_mesh(lhs.proxies[index], rhs.proxies[index])) return false;
    }
    return true;
}

[[nodiscard]] std::size_t internal_node_count(
    const vice::render::VirtualGeometryHierarchy& hierarchy
) {
    return static_cast<std::size_t>(std::count_if(
        hierarchy.nodes.begin(),
        hierarchy.nodes.end(),
        [](const vice::render::VirtualGeometryHierarchyNode& node) {
            return node.child_count != 0U;
        }
    ));
}

void assert_proxy_valid(
    const vice::render::VirtualGeometryProxyMesh& proxy,
    const std::vector<vice::render::MeshVertex>& source_vertices
) {
    assert(proxy.node_index != vice::render::virtual_geometry_invalid_node);
    assert(proxy.source_triangle_count > 0U);
    assert(std::isfinite(proxy.direct_geometric_error));
    assert(proxy.direct_geometric_error >= 0.0F);
    assert(!proxy.vertices.empty());
    assert(!proxy.indices.empty());
    assert(proxy.indices.size() % 3U == 0U);
    for (const auto index : proxy.indices) assert(index < proxy.vertices.size());
    for (std::size_t offset = 0U; offset < proxy.indices.size(); offset += 3U) {
        const auto a = proxy.indices[offset];
        const auto b = proxy.indices[offset + 1U];
        const auto c = proxy.indices[offset + 2U];
        assert(a != b && b != c && a != c);
    }
    for (const auto& proxy_vertex : proxy.vertices) {
        const bool found = std::any_of(
            source_vertices.begin(),
            source_vertices.end(),
            [&](const vice::render::MeshVertex& source_vertex) {
                return same_vertex(proxy_vertex, source_vertex);
            }
        );
        assert(found);
    }
}

void assert_grid_boundaries_survive(
    const vice::render::VirtualGeometryProxyMesh& proxy,
    const ProxyInput& input,
    const std::uint32_t cells_x,
    const std::uint32_t cells_z
) {
    for (const auto& source_vertex : input.vertices) {
        const float x = source_vertex.position[0];
        const float z = source_vertex.position[2];
        const bool boundary = x == 0.0F || z == 0.0F ||
            x == static_cast<float>(cells_x) || z == static_cast<float>(cells_z);
        if (!boundary) continue;
        const bool found = std::any_of(
            proxy.vertices.begin(),
            proxy.vertices.end(),
            [&](const vice::render::MeshVertex& proxy_vertex) {
                return same_vertex(source_vertex, proxy_vertex);
            }
        );
        assert(found);
    }
}

} // namespace

int main() {
    using namespace vice::render;

    constexpr std::uint32_t grid_x = 8U;
    constexpr std::uint32_t grid_z = 8U;
    const auto grid = make_grid(grid_x, grid_z);
    assert(grid.geometry.meshlets.size() == 128U);
    assert(grid.pages.pages.size() == 4U);
    assert(internal_node_count(grid.hierarchy) == 1U);

    const auto grid_proxy = build_virtual_geometry_proxies(
        grid.vertices, grid.geometry, grid.pages, grid.hierarchy
    );
    const auto grid_proxy_repeat = build_virtual_geometry_proxies(
        grid.vertices, grid.geometry, grid.pages, grid.hierarchy
    );
    assert(grid_proxy);
    assert(grid_proxy_repeat);
    assert(same_proxy_set(*grid_proxy.proxy_set, *grid_proxy_repeat.proxy_set));
    assert(grid_proxy.proxy_set->proxies.size() == 1U);

    const auto& root_proxy = grid_proxy.proxy_set->proxies.front();
    assert(root_proxy.node_index == grid.hierarchy.root_index);
    assert(root_proxy.source_triangle_count == 128U);
    assert(root_proxy.indices.size() / 3U < root_proxy.source_triangle_count);
    assert(root_proxy.direct_geometric_error > 0.0F);
    assert_proxy_valid(root_proxy, grid.vertices);
    assert_grid_boundaries_survive(root_proxy, grid, grid_x, grid_z);

    const auto& measured_hierarchy = grid_proxy.proxy_set->hierarchy;
    assert(measured_hierarchy.nodes[measured_hierarchy.root_index].geometric_error >=
        root_proxy.direct_geometric_error);
    assert(measured_hierarchy.nodes[measured_hierarchy.root_index].geometric_error > 0.0F);
    for (const auto& node : measured_hierarchy.nodes) {
        assert(std::isfinite(node.geometric_error));
        assert(node.geometric_error >= 0.0F);
        if (node.child_count == 0U) {
            assert(node.geometric_error == 0.0F);
        } else {
            for (std::uint32_t local = 0U; local < node.child_count; ++local) {
                assert(node.geometric_error >=
                    measured_hierarchy.nodes[node.first_child + local].geometric_error);
            }
        }
    }

    const auto islands = make_triangle_islands(160U);
    assert(islands.pages.pages.size() == 5U);
    assert(internal_node_count(islands.hierarchy) == 2U);
    const auto island_proxy = build_virtual_geometry_proxies(
        islands.vertices, islands.geometry, islands.pages, islands.hierarchy
    );
    assert(island_proxy);
    assert(island_proxy.proxy_set->proxies.size() == internal_node_count(islands.hierarchy));
    for (const auto& proxy : island_proxy.proxy_set->proxies) {
        assert_proxy_valid(proxy, islands.vertices);
        assert(proxy.indices.size() / 3U == proxy.source_triangle_count);
        assert(proxy.direct_geometric_error == 0.0F);
    }
    for (const auto& node : island_proxy.proxy_set->hierarchy.nodes) {
        assert(node.geometric_error == 0.0F);
    }

    auto bad_geometry = grid.geometry;
    bad_geometry.vertex_indices[0] = static_cast<std::uint32_t>(grid.vertices.size());
    assert(!build_virtual_geometry_proxies(
        grid.vertices, bad_geometry, grid.pages, grid.hierarchy
    ));

    auto bad_pages = grid.pages;
    bad_pages.pages[0].meshlet_offset = 1U;
    assert(!build_virtual_geometry_proxies(
        grid.vertices, grid.geometry, bad_pages, grid.hierarchy
    ));

    auto bad_hierarchy = grid.hierarchy;
    assert(bad_hierarchy.nodes.size() > 1U);
    bad_hierarchy.nodes[1].parent_index = virtual_geometry_invalid_node;
    assert(!build_virtual_geometry_proxies(
        grid.vertices, grid.geometry, grid.pages, bad_hierarchy
    ));

    auto bad_vertices = grid.vertices;
    bad_vertices[0].position[0] = std::numeric_limits<float>::infinity();
    assert(!build_virtual_geometry_proxies(
        bad_vertices, grid.geometry, grid.pages, grid.hierarchy
    ));

    return 0;
}
