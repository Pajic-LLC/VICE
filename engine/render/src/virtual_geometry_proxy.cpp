#include <vice/render/virtual_geometry_proxy.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace vice::render {
namespace {

using Triangle = std::array<std::uint32_t, 3U>;
using Edge = std::array<std::uint32_t, 2U>;

struct Topology {
    std::map<Edge, std::uint32_t> edge_counts;
    std::set<std::uint32_t> boundary_vertices;
    bool manifold = true;
};

struct EdgeCandidate {
    Edge edge{};
    double length_squared = 0.0;
};

struct SimplifiedGeometry {
    std::vector<Triangle> triangles;
    double direct_error = 0.0;
};

[[nodiscard]] bool finite_vertex(const MeshVertex& vertex) noexcept {
    for (const float value : vertex.position) {
        if (!std::isfinite(value)) return false;
    }
    for (const float value : vertex.color) {
        if (!std::isfinite(value)) return false;
    }
    for (const float value : vertex.normal) {
        if (!std::isfinite(value)) return false;
    }
    for (const float value : vertex.uv) {
        if (!std::isfinite(value)) return false;
    }
    return true;
}

[[nodiscard]] bool finite_bounds(const MeshletBounds& bounds) noexcept {
    return std::isfinite(bounds.center[0]) &&
        std::isfinite(bounds.center[1]) &&
        std::isfinite(bounds.center[2]) &&
        std::isfinite(bounds.radius) && bounds.radius >= 0.0F;
}

[[nodiscard]] bool contains_bound(
    const MeshletBounds& outer,
    const MeshletBounds& inner
) noexcept {
    if (!finite_bounds(outer) || !finite_bounds(inner)) return false;
    const double dx = static_cast<double>(inner.center[0]) - outer.center[0];
    const double dy = static_cast<double>(inner.center[1]) - outer.center[1];
    const double dz = static_cast<double>(inner.center[2]) - outer.center[2];
    const double distance = std::sqrt(dx * dx + dy * dy + dz * dz);
    const double tolerance = std::max(
        1.0e-4,
        static_cast<double>(outer.radius) * 1.0e-5
    );
    return std::isfinite(distance) &&
        distance + static_cast<double>(inner.radius) <=
            static_cast<double>(outer.radius) + tolerance;
}

[[nodiscard]] Edge make_edge(const std::uint32_t a, const std::uint32_t b) noexcept {
    return a < b ? Edge{a, b} : Edge{b, a};
}

[[nodiscard]] Triangle triangle_key(Triangle triangle) noexcept {
    std::sort(triangle.begin(), triangle.end());
    return triangle;
}

[[nodiscard]] double edge_length_squared(
    const std::span<const MeshVertex> vertices,
    const Edge edge
) noexcept {
    const auto& a = vertices[edge[0]].position;
    const auto& b = vertices[edge[1]].position;
    double result = 0.0;
    for (std::size_t axis = 0U; axis < 3U; ++axis) {
        const double delta = static_cast<double>(a[axis]) - b[axis];
        result += delta * delta;
    }
    return result;
}

[[nodiscard]] Topology build_topology(const std::span<const Triangle> triangles) {
    Topology topology;
    for (const auto& triangle : triangles) {
        const std::array<Edge, 3U> edges{
            make_edge(triangle[0], triangle[1]),
            make_edge(triangle[1], triangle[2]),
            make_edge(triangle[2], triangle[0]),
        };
        for (const auto edge : edges) {
            auto& count = topology.edge_counts[edge];
            ++count;
            if (count > 2U) topology.manifold = false;
        }
    }
    for (const auto& [edge, count] : topology.edge_counts) {
        if (count == 1U) {
            topology.boundary_vertices.insert(edge[0]);
            topology.boundary_vertices.insert(edge[1]);
        }
    }
    return topology;
}

[[nodiscard]] bool orientation_preserved(
    const Triangle& before,
    const Triangle& after,
    const std::span<const MeshVertex> vertices
) noexcept {
    const auto normal = [&](const Triangle& triangle) {
        std::array<double, 3U> result{};
        const auto& a = vertices[triangle[0]].position;
        const auto& b = vertices[triangle[1]].position;
        const auto& c = vertices[triangle[2]].position;
        const double abx = static_cast<double>(b[0]) - a[0];
        const double aby = static_cast<double>(b[1]) - a[1];
        const double abz = static_cast<double>(b[2]) - a[2];
        const double acx = static_cast<double>(c[0]) - a[0];
        const double acy = static_cast<double>(c[1]) - a[1];
        const double acz = static_cast<double>(c[2]) - a[2];
        result[0] = aby * acz - abz * acy;
        result[1] = abz * acx - abx * acz;
        result[2] = abx * acy - aby * acx;
        return result;
    };

    const auto before_normal = normal(before);
    const auto after_normal = normal(after);
    const double before_length_squared =
        before_normal[0] * before_normal[0] +
        before_normal[1] * before_normal[1] +
        before_normal[2] * before_normal[2];
    const double after_length_squared =
        after_normal[0] * after_normal[0] +
        after_normal[1] * after_normal[1] +
        after_normal[2] * after_normal[2];
    if (!std::isfinite(before_length_squared) || !std::isfinite(after_length_squared)) {
        return false;
    }
    constexpr double area_epsilon = 1.0e-20;
    if (before_length_squared <= area_epsilon) return true;
    if (after_length_squared <= area_epsilon) return false;
    const double dot =
        before_normal[0] * after_normal[0] +
        before_normal[1] * after_normal[1] +
        before_normal[2] * after_normal[2];
    return std::isfinite(dot) && dot > 0.0;
}

[[nodiscard]] std::optional<std::vector<Triangle>> collapse_edge(
    const std::span<const Triangle> triangles,
    const std::span<const MeshVertex> vertices,
    const Edge edge,
    const std::set<std::uint32_t>& expected_boundary
) {
    const std::uint32_t keep = edge[0];
    const std::uint32_t remove = edge[1];
    std::vector<Triangle> collapsed;
    collapsed.reserve(triangles.size());
    std::set<Triangle> seen;

    for (const auto& triangle : triangles) {
        Triangle replacement = triangle;
        bool changed = false;
        for (auto& index : replacement) {
            if (index == remove) {
                index = keep;
                changed = true;
            }
        }
        if (replacement[0] == replacement[1] || replacement[1] == replacement[2] ||
            replacement[0] == replacement[2]) {
            continue;
        }
        if (changed && !orientation_preserved(triangle, replacement, vertices)) {
            return std::nullopt;
        }
        const auto key = triangle_key(replacement);
        if (!seen.insert(key).second) continue;
        collapsed.push_back(replacement);
    }

    if (collapsed.empty() || collapsed.size() >= triangles.size()) return std::nullopt;
    const auto topology = build_topology(collapsed);
    if (!topology.manifold || topology.boundary_vertices != expected_boundary) {
        return std::nullopt;
    }
    return collapsed;
}

[[nodiscard]] std::optional<SimplifiedGeometry> simplify_triangles(
    const std::span<const MeshVertex> vertices,
    const std::span<const Triangle> source_triangles
) {
    if (source_triangles.empty()) return std::nullopt;

    SimplifiedGeometry simplified;
    simplified.triangles.assign(source_triangles.begin(), source_triangles.end());
    std::map<std::uint32_t, double> cluster_errors;
    for (const auto& triangle : simplified.triangles) {
        for (const auto index : triangle) cluster_errors.try_emplace(index, 0.0);
    }

    const std::size_t target_triangle_count = std::max<std::size_t>(
        1U,
        (simplified.triangles.size() + virtual_geometry_proxy_triangle_divisor - 1U) /
            virtual_geometry_proxy_triangle_divisor
    );

    while (simplified.triangles.size() > target_triangle_count) {
        const auto topology = build_topology(simplified.triangles);
        if (!topology.manifold) break;

        std::vector<EdgeCandidate> candidates;
        candidates.reserve(topology.edge_counts.size());
        for (const auto& [edge, count] : topology.edge_counts) {
            if (count != 2U || topology.boundary_vertices.contains(edge[0]) ||
                topology.boundary_vertices.contains(edge[1])) {
                continue;
            }
            const double length_squared = edge_length_squared(vertices, edge);
            if (!std::isfinite(length_squared) || length_squared < 0.0) {
                return std::nullopt;
            }
            candidates.push_back(EdgeCandidate{.edge = edge, .length_squared = length_squared});
        }
        std::sort(
            candidates.begin(),
            candidates.end(),
            [](const EdgeCandidate& lhs, const EdgeCandidate& rhs) {
                if (lhs.length_squared != rhs.length_squared) {
                    return lhs.length_squared < rhs.length_squared;
                }
                return lhs.edge < rhs.edge;
            }
        );

        bool made_progress = false;
        for (const auto& candidate : candidates) {
            const auto collapsed = collapse_edge(
                simplified.triangles,
                vertices,
                candidate.edge,
                topology.boundary_vertices
            );
            if (!collapsed.has_value()) continue;

            const auto keep = candidate.edge[0];
            const auto remove = candidate.edge[1];
            const auto keep_error = cluster_errors.find(keep);
            const auto remove_error = cluster_errors.find(remove);
            if (keep_error == cluster_errors.end() || remove_error == cluster_errors.end()) {
                return std::nullopt;
            }
            const double edge_length = std::sqrt(candidate.length_squared);
            const double merged_error = std::max(
                keep_error->second,
                remove_error->second + edge_length
            );
            if (!std::isfinite(merged_error)) return std::nullopt;
            keep_error->second = merged_error;
            cluster_errors.erase(remove_error);
            simplified.direct_error = std::max(simplified.direct_error, merged_error);
            simplified.triangles = std::move(*collapsed);
            made_progress = true;
            break;
        }
        if (!made_progress) break;
    }

    return simplified;
}

[[nodiscard]] bool validate_inputs(
    const std::span<const MeshVertex> vertices,
    const MeshletGeometry& geometry,
    const VirtualGeometryPageTable& page_table,
    const VirtualGeometryHierarchy& hierarchy,
    std::string& diagnostics
) {
    if (vertices.empty() || vertices.size() > std::numeric_limits<std::uint32_t>::max()) {
        diagnostics = "Virtual geometry proxy input requires a non-empty 32-bit vertex stream.";
        return false;
    }
    for (const auto& vertex : vertices) {
        if (!finite_vertex(vertex)) {
            diagnostics = "Virtual geometry proxy input contains a non-finite vertex.";
            return false;
        }
    }
    if (geometry.meshlets.empty() || geometry.vertex_indices.empty() ||
        geometry.triangle_indices.empty()) {
        diagnostics = "Virtual geometry proxy input requires canonical meshlet streams.";
        return false;
    }
    if (geometry.meshlets.size() > std::numeric_limits<std::uint32_t>::max() ||
        geometry.vertex_indices.size() > std::numeric_limits<std::uint32_t>::max() ||
        geometry.triangle_indices.size() > std::numeric_limits<std::uint32_t>::max()) {
        diagnostics = "Virtual geometry proxy meshlet streams exceed the 32-bit contract.";
        return false;
    }

    std::size_t expected_vertex_offset = 0U;
    std::size_t expected_triangle_offset = 0U;
    for (const auto& meshlet : geometry.meshlets) {
        if (meshlet.vertex_count == 0U || meshlet.vertex_count > meshlet_max_vertices ||
            meshlet.triangle_count == 0U || meshlet.triangle_count > meshlet_max_triangles ||
            meshlet.vertex_offset != expected_vertex_offset ||
            meshlet.triangle_offset != expected_triangle_offset || !finite_bounds(meshlet.bounds)) {
            diagnostics = "Virtual geometry proxy input contains an invalid or non-canonical meshlet.";
            return false;
        }
        const std::size_t vertex_end = expected_vertex_offset + meshlet.vertex_count;
        const std::size_t triangle_end = expected_triangle_offset +
            static_cast<std::size_t>(meshlet.triangle_count) * 3U;
        if (vertex_end > geometry.vertex_indices.size() ||
            triangle_end > geometry.triangle_indices.size()) {
            diagnostics = "Virtual geometry proxy meshlet ranges exceed their backing streams.";
            return false;
        }
        for (std::size_t offset = expected_vertex_offset; offset < vertex_end; ++offset) {
            if (geometry.vertex_indices[offset] >= vertices.size()) {
                diagnostics = "Virtual geometry proxy meshlet remap contains an out-of-range vertex.";
                return false;
            }
        }
        for (std::uint32_t triangle = 0U; triangle < meshlet.triangle_count; ++triangle) {
            const std::size_t offset = expected_triangle_offset +
                static_cast<std::size_t>(triangle) * 3U;
            const auto a = geometry.triangle_indices[offset];
            const auto b = geometry.triangle_indices[offset + 1U];
            const auto c = geometry.triangle_indices[offset + 2U];
            if (a >= meshlet.vertex_count || b >= meshlet.vertex_count || c >= meshlet.vertex_count) {
                diagnostics = "Virtual geometry proxy meshlet contains an invalid local triangle index.";
                return false;
            }
            const auto ga = geometry.vertex_indices[expected_vertex_offset + a];
            const auto gb = geometry.vertex_indices[expected_vertex_offset + b];
            const auto gc = geometry.vertex_indices[expected_vertex_offset + c];
            if (ga == gb || gb == gc || ga == gc) {
                diagnostics = "Virtual geometry proxy meshlet resolves to a degenerate source triangle.";
                return false;
            }
        }
        expected_vertex_offset = vertex_end;
        expected_triangle_offset = triangle_end;
    }
    if (expected_vertex_offset != geometry.vertex_indices.size() ||
        expected_triangle_offset != geometry.triangle_indices.size()) {
        diagnostics = "Virtual geometry proxy meshlet streams contain trailing or missing data.";
        return false;
    }

    if (page_table.pages.empty() ||
        page_table.pages.size() > std::numeric_limits<std::uint32_t>::max()) {
        diagnostics = "Virtual geometry proxy input requires a non-empty 32-bit page table.";
        return false;
    }
    std::size_t expected_meshlet_offset = 0U;
    for (std::size_t page_index = 0U; page_index < page_table.pages.size(); ++page_index) {
        const auto& page = page_table.pages[page_index];
        if (page.meshlet_count == 0U || page.meshlet_count > virtual_geometry_page_max_meshlets ||
            page.meshlet_offset != expected_meshlet_offset || !finite_bounds(page.bounds)) {
            diagnostics = "Virtual geometry proxy input contains an invalid or non-canonical page.";
            return false;
        }
        if (page_index + 1U < page_table.pages.size() &&
            page.meshlet_count != virtual_geometry_page_max_meshlets) {
            diagnostics = "Virtual geometry proxy non-final pages must use the canonical meshlet count.";
            return false;
        }
        const std::size_t meshlet_end = expected_meshlet_offset + page.meshlet_count;
        if (meshlet_end > geometry.meshlets.size()) {
            diagnostics = "Virtual geometry proxy page meshlet range exceeds the meshlet stream.";
            return false;
        }
        const auto& first = geometry.meshlets[expected_meshlet_offset];
        const auto& last = geometry.meshlets[meshlet_end - 1U];
        const std::uint64_t vertex_end = static_cast<std::uint64_t>(last.vertex_offset) + last.vertex_count;
        const std::uint64_t triangle_end = static_cast<std::uint64_t>(last.triangle_offset) +
            static_cast<std::uint64_t>(last.triangle_count) * 3U;
        if (page.vertex_index_offset != first.vertex_offset ||
            page.vertex_index_count != vertex_end - first.vertex_offset ||
            page.triangle_index_offset != first.triangle_offset ||
            page.triangle_index_count != triangle_end - first.triangle_offset) {
            diagnostics = "Virtual geometry proxy page stream ranges are not canonical.";
            return false;
        }
        for (std::size_t meshlet_index = expected_meshlet_offset;
             meshlet_index < meshlet_end; ++meshlet_index) {
            if (!contains_bound(page.bounds, geometry.meshlets[meshlet_index].bounds)) {
                diagnostics = "Virtual geometry proxy page bounds do not contain a member meshlet.";
                return false;
            }
        }
        expected_meshlet_offset = meshlet_end;
    }
    if (expected_meshlet_offset != geometry.meshlets.size()) {
        diagnostics = "Virtual geometry proxy page table does not cover every meshlet exactly once.";
        return false;
    }

    if (hierarchy.nodes.empty() || hierarchy.root_index != 0U ||
        hierarchy.nodes.size() > std::numeric_limits<std::uint32_t>::max()) {
        diagnostics = "Virtual geometry proxy input requires a non-empty root-first hierarchy.";
        return false;
    }
    const auto& root = hierarchy.nodes.front();
    if (root.parent_index != virtual_geometry_invalid_node || root.depth != 0U ||
        root.page_offset != 0U || root.page_count != page_table.pages.size()) {
        diagnostics = "Virtual geometry proxy hierarchy has an invalid root contract.";
        return false;
    }

    std::vector<std::uint32_t> incoming(hierarchy.nodes.size(), 0U);
    std::vector<std::uint32_t> leaf_pages;
    leaf_pages.reserve(page_table.pages.size());
    for (std::size_t index = 0U; index < hierarchy.nodes.size(); ++index) {
        const auto& node = hierarchy.nodes[index];
        if (node.page_count == 0U ||
            static_cast<std::uint64_t>(node.page_offset) + node.page_count > page_table.pages.size() ||
            !finite_bounds(node.bounds) || !std::isfinite(node.geometric_error) ||
            node.geometric_error < 0.0F) {
            diagnostics = "Virtual geometry proxy hierarchy contains invalid node metadata.";
            return false;
        }
        if (index != hierarchy.root_index &&
            (node.parent_index == virtual_geometry_invalid_node || node.parent_index >= index)) {
            diagnostics = "Virtual geometry proxy hierarchy has a non-canonical parent index.";
            return false;
        }

        if (node.child_count == 0U) {
            if (node.first_child != virtual_geometry_invalid_node || node.page_count != 1U ||
                !contains_bound(node.bounds, page_table.pages[node.page_offset].bounds)) {
                diagnostics = "Virtual geometry proxy hierarchy leaf contract is invalid.";
                return false;
            }
            leaf_pages.push_back(node.page_offset);
            continue;
        }
        if (node.child_count > virtual_geometry_hierarchy_fanout ||
            node.first_child == virtual_geometry_invalid_node || node.first_child <= index ||
            static_cast<std::uint64_t>(node.first_child) + node.child_count > hierarchy.nodes.size()) {
            diagnostics = "Virtual geometry proxy hierarchy contains an invalid child range.";
            return false;
        }
        std::uint64_t expected_page = node.page_offset;
        std::uint64_t covered_pages = 0U;
        for (std::uint32_t local = 0U; local < node.child_count; ++local) {
            const auto child_index = node.first_child + local;
            const auto& child = hierarchy.nodes[child_index];
            if (child.parent_index != index || child.depth != node.depth + 1U ||
                child.page_offset != expected_page || !contains_bound(node.bounds, child.bounds)) {
                diagnostics = "Virtual geometry proxy hierarchy child connectivity or bounds are invalid.";
                return false;
            }
            ++incoming[child_index];
            expected_page += child.page_count;
            covered_pages += child.page_count;
        }
        if (covered_pages != node.page_count ||
            expected_page != static_cast<std::uint64_t>(node.page_offset) + node.page_count) {
            diagnostics = "Virtual geometry proxy hierarchy children do not exactly cover their parent.";
            return false;
        }
    }
    for (std::size_t index = 1U; index < incoming.size(); ++index) {
        if (incoming[index] != 1U) {
            diagnostics = "Virtual geometry proxy hierarchy contains disconnected or repeated nodes.";
            return false;
        }
    }
    if (leaf_pages.size() != page_table.pages.size()) {
        diagnostics = "Virtual geometry proxy hierarchy leaves do not cover the page table exactly once.";
        return false;
    }
    std::sort(leaf_pages.begin(), leaf_pages.end());
    for (std::size_t index = 0U; index < leaf_pages.size(); ++index) {
        if (leaf_pages[index] != index) {
            diagnostics = "Virtual geometry proxy hierarchy leaf pages are gapped or overlapping.";
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::vector<Triangle> extract_node_triangles(
    const MeshletGeometry& geometry,
    const VirtualGeometryPageTable& page_table,
    const VirtualGeometryHierarchyNode& node
) {
    std::vector<Triangle> triangles;
    for (std::uint32_t page_local = 0U; page_local < node.page_count; ++page_local) {
        const auto& page = page_table.pages[node.page_offset + page_local];
        for (std::uint32_t meshlet_local = 0U; meshlet_local < page.meshlet_count; ++meshlet_local) {
            const auto& meshlet = geometry.meshlets[page.meshlet_offset + meshlet_local];
            for (std::uint32_t triangle = 0U; triangle < meshlet.triangle_count; ++triangle) {
                const std::size_t triangle_offset = static_cast<std::size_t>(meshlet.triangle_offset) +
                    static_cast<std::size_t>(triangle) * 3U;
                Triangle output{};
                for (std::size_t corner = 0U; corner < 3U; ++corner) {
                    const auto local_index = geometry.triangle_indices[triangle_offset + corner];
                    output[corner] = geometry.vertex_indices[
                        static_cast<std::size_t>(meshlet.vertex_offset) + local_index
                    ];
                }
                triangles.push_back(output);
            }
        }
    }
    return triangles;
}

[[nodiscard]] std::optional<VirtualGeometryProxyMesh> make_proxy(
    const std::uint32_t node_index,
    const std::span<const MeshVertex> vertices,
    const std::span<const Triangle> source_triangles
) {
    if (source_triangles.empty() ||
        source_triangles.size() > std::numeric_limits<std::uint32_t>::max()) {
        return std::nullopt;
    }
    const auto simplified = simplify_triangles(vertices, source_triangles);
    if (!simplified.has_value() || simplified->triangles.empty() ||
        !std::isfinite(simplified->direct_error) || simplified->direct_error < 0.0 ||
        simplified->direct_error > std::numeric_limits<float>::max()) {
        return std::nullopt;
    }

    VirtualGeometryProxyMesh proxy;
    proxy.node_index = node_index;
    proxy.source_triangle_count = static_cast<std::uint32_t>(source_triangles.size());
    proxy.direct_geometric_error = static_cast<float>(simplified->direct_error);
    proxy.indices.reserve(simplified->triangles.size() * 3U);

    std::map<std::uint32_t, std::uint32_t> local_indices;
    for (const auto& triangle : simplified->triangles) {
        for (const auto global_index : triangle) {
            auto found = local_indices.find(global_index);
            if (found == local_indices.end()) {
                if (proxy.vertices.size() >= std::numeric_limits<std::uint32_t>::max()) {
                    return std::nullopt;
                }
                const auto local_index = static_cast<std::uint32_t>(proxy.vertices.size());
                proxy.vertices.push_back(vertices[global_index]);
                found = local_indices.emplace(global_index, local_index).first;
            }
            proxy.indices.push_back(found->second);
        }
    }
    if (proxy.vertices.empty() || proxy.indices.empty() || proxy.indices.size() % 3U != 0U) {
        return std::nullopt;
    }
    return proxy;
}

} // namespace

VirtualGeometryProxyBuildResult build_virtual_geometry_proxies(
    const std::span<const MeshVertex> vertices,
    const MeshletGeometry& geometry,
    const VirtualGeometryPageTable& page_table,
    const VirtualGeometryHierarchy& hierarchy
) {
    VirtualGeometryProxyBuildResult result;
    if (!validate_inputs(vertices, geometry, page_table, hierarchy, result.diagnostics)) {
        return result;
    }

    VirtualGeometryProxySet proxy_set;
    proxy_set.hierarchy = hierarchy;
    std::vector<float> direct_errors(hierarchy.nodes.size(), 0.0F);

    std::size_t internal_node_count = 0U;
    for (const auto& node : hierarchy.nodes) {
        if (node.child_count != 0U) ++internal_node_count;
    }
    proxy_set.proxies.reserve(internal_node_count);

    for (std::size_t node_index = 0U; node_index < hierarchy.nodes.size(); ++node_index) {
        const auto& node = hierarchy.nodes[node_index];
        if (node.child_count == 0U) continue;
        const auto source_triangles = extract_node_triangles(geometry, page_table, node);
        const auto proxy = make_proxy(
            static_cast<std::uint32_t>(node_index),
            vertices,
            source_triangles
        );
        if (!proxy.has_value()) {
            result.diagnostics = "Virtual geometry proxy simplification could not produce a valid renderable payload.";
            return result;
        }
        direct_errors[node_index] = proxy->direct_geometric_error;
        proxy_set.proxies.push_back(*proxy);
    }

    for (std::size_t reverse = proxy_set.hierarchy.nodes.size(); reverse > 0U; --reverse) {
        const std::size_t node_index = reverse - 1U;
        auto& node = proxy_set.hierarchy.nodes[node_index];
        if (node.child_count == 0U) {
            node.geometric_error = 0.0F;
            continue;
        }
        float propagated_error = direct_errors[node_index];
        for (std::uint32_t local = 0U; local < node.child_count; ++local) {
            propagated_error = std::max(
                propagated_error,
                proxy_set.hierarchy.nodes[node.first_child + local].geometric_error
            );
        }
        if (!std::isfinite(propagated_error) || propagated_error < 0.0F) {
            result.diagnostics = "Virtual geometry proxy error propagation produced an invalid value.";
            return result;
        }
        node.geometric_error = propagated_error;
    }

    if (proxy_set.proxies.size() != internal_node_count) {
        result.diagnostics = "Virtual geometry proxy generation did not cover every internal hierarchy node.";
        return result;
    }
    result.proxy_set = std::move(proxy_set);
    return result;
}

} // namespace vice::render
