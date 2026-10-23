#include <vice/render/meshlet.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <span>
#include <vector>

namespace vice::render {
namespace {

[[nodiscard]] bool finite_position(const MeshVertex& vertex) noexcept {
    return std::isfinite(vertex.position[0]) &&
        std::isfinite(vertex.position[1]) &&
        std::isfinite(vertex.position[2]);
}

[[nodiscard]] bool finite_bounds(const MeshletBounds& bounds) noexcept {
    return std::isfinite(bounds.center[0]) &&
        std::isfinite(bounds.center[1]) &&
        std::isfinite(bounds.center[2]) &&
        std::isfinite(bounds.radius) && bounds.radius >= 0.0F;
}

[[nodiscard]] std::optional<MeshletBounds> meshlet_bounds(
    const std::span<const MeshVertex> vertices,
    const std::span<const std::uint32_t> vertex_indices
) noexcept {
    if (vertex_indices.empty()) return std::nullopt;

    std::array<double, 3U> minimum{
        std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::infinity(),
    };
    std::array<double, 3U> maximum{
        -std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity(),
    };

    for (const std::uint32_t vertex_index : vertex_indices) {
        if (vertex_index >= vertices.size()) return std::nullopt;
        const auto& vertex = vertices[vertex_index];
        if (!finite_position(vertex)) return std::nullopt;
        for (std::size_t axis = 0U; axis < 3U; ++axis) {
            const double value = static_cast<double>(vertex.position[axis]);
            minimum[axis] = std::min(minimum[axis], value);
            maximum[axis] = std::max(maximum[axis], value);
        }
    }

    std::array<double, 3U> center{};
    constexpr double max_float = static_cast<double>(std::numeric_limits<float>::max());
    for (std::size_t axis = 0U; axis < 3U; ++axis) {
        center[axis] = minimum[axis] + (maximum[axis] - minimum[axis]) * 0.5;
        if (!std::isfinite(center[axis]) || std::abs(center[axis]) > max_float) {
            return std::nullopt;
        }
    }

    double radius_squared = 0.0;
    for (const std::uint32_t vertex_index : vertex_indices) {
        const auto& vertex = vertices[vertex_index];
        double distance_squared = 0.0;
        for (std::size_t axis = 0U; axis < 3U; ++axis) {
            const double delta = static_cast<double>(vertex.position[axis]) - center[axis];
            distance_squared += delta * delta;
        }
        radius_squared = std::max(radius_squared, distance_squared);
    }

    const double radius = std::sqrt(radius_squared);
    if (!std::isfinite(radius) || radius > max_float) return std::nullopt;

    return MeshletBounds{
        .center = {
            static_cast<float>(center[0]),
            static_cast<float>(center[1]),
            static_cast<float>(center[2]),
        },
        .radius = static_cast<float>(radius),
    };
}

[[nodiscard]] std::optional<MeshletBounds> aggregate_bounds(
    const std::span<const MeshletBounds> bounds
) noexcept {
    if (bounds.empty()) return std::nullopt;

    std::array<double, 3U> minimum{
        std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::infinity(),
    };
    std::array<double, 3U> maximum{
        -std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity(),
    };

    for (const auto& bound : bounds) {
        if (!finite_bounds(bound)) return std::nullopt;
        const double radius = static_cast<double>(bound.radius);
        for (std::size_t axis = 0U; axis < 3U; ++axis) {
            const double center = static_cast<double>(bound.center[axis]);
            minimum[axis] = std::min(minimum[axis], center - radius);
            maximum[axis] = std::max(maximum[axis], center + radius);
        }
    }

    std::array<double, 3U> center{};
    constexpr double max_float = static_cast<double>(std::numeric_limits<float>::max());
    for (std::size_t axis = 0U; axis < 3U; ++axis) {
        center[axis] = minimum[axis] + (maximum[axis] - minimum[axis]) * 0.5;
        if (!std::isfinite(center[axis]) || std::abs(center[axis]) > max_float) {
            return std::nullopt;
        }
    }

    double radius = 0.0;
    for (const auto& bound : bounds) {
        double distance_squared = 0.0;
        for (std::size_t axis = 0U; axis < 3U; ++axis) {
            const double delta = static_cast<double>(bound.center[axis]) - center[axis];
            distance_squared += delta * delta;
        }
        const double candidate = std::sqrt(distance_squared) +
            static_cast<double>(bound.radius);
        radius = std::max(radius, candidate);
    }
    if (!std::isfinite(radius) || radius > max_float) return std::nullopt;

    return MeshletBounds{
        .center = {
            static_cast<float>(center[0]),
            static_cast<float>(center[1]),
            static_cast<float>(center[2]),
        },
        .radius = static_cast<float>(radius),
    };
}

[[nodiscard]] std::optional<MeshletBounds> aggregate_bounds(
    const std::span<const Meshlet> meshlets
) noexcept {
    if (meshlets.empty()) return std::nullopt;
    std::vector<MeshletBounds> bounds;
    bounds.reserve(meshlets.size());
    for (const auto& meshlet : meshlets) bounds.push_back(meshlet.bounds);
    return aggregate_bounds(bounds);
}

struct HierarchyBuildNode {
    std::uint32_t page_offset = 0U;
    std::uint32_t page_count = 0U;
    MeshletBounds bounds{};
    float geometric_error = 0.0F;
};

[[nodiscard]] std::optional<float> hierarchy_parent_error(
    const MeshletBounds& parent_bounds,
    const std::span<const HierarchyBuildNode> children
) noexcept {
    if (!finite_bounds(parent_bounds) || children.empty()) return std::nullopt;
    double error = 0.0;
    constexpr double max_float = static_cast<double>(std::numeric_limits<float>::max());
    for (const auto& child : children) {
        if (!finite_bounds(child.bounds) || !std::isfinite(child.geometric_error) ||
            child.geometric_error < 0.0F) {
            return std::nullopt;
        }
        double distance_squared = 0.0;
        for (std::size_t axis = 0U; axis < 3U; ++axis) {
            const double delta = static_cast<double>(child.bounds.center[axis]) -
                parent_bounds.center[axis];
            distance_squared += delta * delta;
        }
        const double extent_error = std::sqrt(distance_squared) +
            static_cast<double>(child.bounds.radius);
        error = std::max(error, std::max(extent_error, static_cast<double>(child.geometric_error)));
    }
    if (!std::isfinite(error) || error > max_float) return std::nullopt;
    return static_cast<float>(error);
}

} // namespace

MeshletBuildResult build_meshlets(
    const std::span<const MeshVertex> vertices,
    const std::span<const std::uint32_t> indices
) {
    MeshletBuildResult result;
    if (vertices.empty() || indices.empty() || indices.size() % 3U != 0U) {
        result.diagnostics = "Meshlet input must contain indexed triangle geometry.";
        return result;
    }
    if (vertices.size() > std::numeric_limits<std::uint32_t>::max() ||
        indices.size() > std::numeric_limits<std::uint32_t>::max()) {
        result.diagnostics = "Meshlet input exceeds the 32-bit geometry contract.";
        return result;
    }
    for (const auto& vertex : vertices) {
        if (!finite_position(vertex)) {
            result.diagnostics = "Meshlet input contains a non-finite vertex position.";
            return result;
        }
    }
    for (std::size_t offset = 0U; offset < indices.size(); offset += 3U) {
        const std::uint32_t a = indices[offset];
        const std::uint32_t b = indices[offset + 1U];
        const std::uint32_t c = indices[offset + 2U];
        if (a >= vertices.size() || b >= vertices.size() || c >= vertices.size()) {
            result.diagnostics = "Meshlet input contains an out-of-range triangle index.";
            return result;
        }
        if (a == b || b == c || a == c) {
            result.diagnostics = "Meshlet input contains a degenerate indexed triangle.";
            return result;
        }
    }

    MeshletGeometry geometry;
    geometry.meshlets.reserve((indices.size() / 3U + meshlet_max_triangles - 1U) /
        meshlet_max_triangles);
    geometry.vertex_indices.reserve(indices.size());
    geometry.triangle_indices.reserve(indices.size());

    std::vector<std::uint32_t> current_vertices;
    std::vector<std::uint8_t> current_triangles;
    current_vertices.reserve(meshlet_max_vertices);
    current_triangles.reserve(meshlet_max_triangles * 3U);

    const auto flush = [&]() -> bool {
        if (current_triangles.empty()) return true;
        if (geometry.vertex_indices.size() > std::numeric_limits<std::uint32_t>::max() ||
            geometry.triangle_indices.size() > std::numeric_limits<std::uint32_t>::max()) {
            result.diagnostics = "Meshlet output exceeds the 32-bit offset contract.";
            return false;
        }
        const auto bounds = meshlet_bounds(vertices, current_vertices);
        if (!bounds.has_value()) {
            result.diagnostics = "Meshlet builder could not produce finite conservative bounds.";
            return false;
        }

        geometry.meshlets.push_back(Meshlet{
            .vertex_offset = static_cast<std::uint32_t>(geometry.vertex_indices.size()),
            .vertex_count = static_cast<std::uint32_t>(current_vertices.size()),
            .triangle_offset = static_cast<std::uint32_t>(geometry.triangle_indices.size()),
            .triangle_count = static_cast<std::uint32_t>(current_triangles.size() / 3U),
            .bounds = *bounds,
        });
        geometry.vertex_indices.insert(
            geometry.vertex_indices.end(), current_vertices.begin(), current_vertices.end()
        );
        geometry.triangle_indices.insert(
            geometry.triangle_indices.end(), current_triangles.begin(), current_triangles.end()
        );
        current_vertices.clear();
        current_triangles.clear();
        return true;
    };

    for (std::size_t offset = 0U; offset < indices.size(); offset += 3U) {
        const std::array<std::uint32_t, 3U> triangle{
            indices[offset], indices[offset + 1U], indices[offset + 2U],
        };

        std::uint32_t new_vertex_count = 0U;
        for (const std::uint32_t vertex_index : triangle) {
            if (std::find(current_vertices.begin(), current_vertices.end(), vertex_index) ==
                current_vertices.end()) {
                ++new_vertex_count;
            }
        }
        const std::size_t current_triangle_count = current_triangles.size() / 3U;
        if (!current_triangles.empty() &&
            (current_triangle_count >= meshlet_max_triangles ||
             current_vertices.size() + new_vertex_count > meshlet_max_vertices)) {
            if (!flush()) return result;
        }

        for (const std::uint32_t vertex_index : triangle) {
            auto found = std::find(current_vertices.begin(), current_vertices.end(), vertex_index);
            if (found == current_vertices.end()) {
                current_vertices.push_back(vertex_index);
                found = std::prev(current_vertices.end());
            }
            const auto local_index = static_cast<std::size_t>(
                std::distance(current_vertices.begin(), found)
            );
            if (local_index >= meshlet_max_vertices ||
                local_index > std::numeric_limits<std::uint8_t>::max()) {
                result.diagnostics = "Meshlet builder produced an invalid local vertex index.";
                return result;
            }
            current_triangles.push_back(static_cast<std::uint8_t>(local_index));
        }
    }

    if (!flush()) return result;
    if (geometry.meshlets.empty()) {
        result.diagnostics = "Meshlet builder produced no clusters.";
        return result;
    }

    result.geometry = std::move(geometry);
    return result;
}

VirtualGeometryPageBuildResult build_virtual_geometry_pages(const MeshletGeometry& geometry) {
    VirtualGeometryPageBuildResult result;
    if (geometry.meshlets.empty() || geometry.vertex_indices.empty() ||
        geometry.triangle_indices.empty()) {
        result.diagnostics = "Virtual geometry input must contain meshlet descriptor and index streams.";
        return result;
    }
    if (geometry.meshlets.size() > std::numeric_limits<std::uint32_t>::max() ||
        geometry.vertex_indices.size() > std::numeric_limits<std::uint32_t>::max() ||
        geometry.triangle_indices.size() > std::numeric_limits<std::uint32_t>::max()) {
        result.diagnostics = "Virtual geometry input exceeds the 32-bit page range contract.";
        return result;
    }

    std::size_t expected_vertex_offset = 0U;
    std::size_t expected_triangle_offset = 0U;
    for (const auto& meshlet : geometry.meshlets) {
        if (meshlet.vertex_count == 0U || meshlet.vertex_count > meshlet_max_vertices ||
            meshlet.triangle_count == 0U || meshlet.triangle_count > meshlet_max_triangles) {
            result.diagnostics = "Virtual geometry input contains an invalid meshlet count.";
            return result;
        }
        if (meshlet.vertex_offset != expected_vertex_offset ||
            meshlet.triangle_offset != expected_triangle_offset) {
            result.diagnostics = "Virtual geometry meshlet ranges must be canonical and contiguous.";
            return result;
        }
        if (!finite_bounds(meshlet.bounds)) {
            result.diagnostics = "Virtual geometry input contains invalid meshlet bounds.";
            return result;
        }

        const std::size_t vertex_end = expected_vertex_offset + meshlet.vertex_count;
        const std::size_t triangle_count = static_cast<std::size_t>(meshlet.triangle_count) * 3U;
        const std::size_t triangle_end = expected_triangle_offset + triangle_count;
        if (vertex_end > geometry.vertex_indices.size() ||
            triangle_end > geometry.triangle_indices.size()) {
            result.diagnostics = "Virtual geometry meshlet ranges exceed their backing streams.";
            return result;
        }
        for (std::size_t offset = expected_triangle_offset; offset < triangle_end; ++offset) {
            if (geometry.triangle_indices[offset] >= meshlet.vertex_count) {
                result.diagnostics = "Virtual geometry input contains an invalid local meshlet index.";
                return result;
            }
        }

        expected_vertex_offset = vertex_end;
        expected_triangle_offset = triangle_end;
    }
    if (expected_vertex_offset != geometry.vertex_indices.size() ||
        expected_triangle_offset != geometry.triangle_indices.size()) {
        result.diagnostics = "Virtual geometry input contains trailing or missing meshlet stream data.";
        return result;
    }

    VirtualGeometryPageTable page_table;
    page_table.pages.reserve(
        (geometry.meshlets.size() + virtual_geometry_page_max_meshlets - 1U) /
        virtual_geometry_page_max_meshlets
    );
    const std::span<const Meshlet> meshlets{
        geometry.meshlets.data(), geometry.meshlets.size()
    };
    for (std::size_t meshlet_offset = 0U; meshlet_offset < meshlets.size();
         meshlet_offset += virtual_geometry_page_max_meshlets) {
        const std::size_t meshlet_count = std::min<std::size_t>(
            virtual_geometry_page_max_meshlets,
            meshlets.size() - meshlet_offset
        );
        const auto page_meshlets = meshlets.subspan(meshlet_offset, meshlet_count);
        const auto bounds = aggregate_bounds(page_meshlets);
        if (!bounds.has_value()) {
            result.diagnostics = "Virtual geometry page bounds could not be represented finitely.";
            return result;
        }

        const auto& first = page_meshlets.front();
        const auto& last = page_meshlets.back();
        const std::size_t vertex_end = static_cast<std::size_t>(last.vertex_offset) +
            last.vertex_count;
        const std::size_t triangle_end = static_cast<std::size_t>(last.triangle_offset) +
            static_cast<std::size_t>(last.triangle_count) * 3U;
        page_table.pages.push_back(VirtualGeometryPage{
            .meshlet_offset = static_cast<std::uint32_t>(meshlet_offset),
            .meshlet_count = static_cast<std::uint32_t>(meshlet_count),
            .vertex_index_offset = first.vertex_offset,
            .vertex_index_count = static_cast<std::uint32_t>(
                vertex_end - first.vertex_offset
            ),
            .triangle_index_offset = first.triangle_offset,
            .triangle_index_count = static_cast<std::uint32_t>(
                triangle_end - first.triangle_offset
            ),
            .bounds = *bounds,
        });
    }

    result.page_table = std::move(page_table);
    return result;
}

VirtualGeometryHierarchyBuildResult build_virtual_geometry_hierarchy(
    const VirtualGeometryPageTable& page_table
) {
    VirtualGeometryHierarchyBuildResult result;
    const auto& pages = page_table.pages;
    if (pages.empty()) {
        result.diagnostics = "Virtual geometry hierarchy input must contain at least one page.";
        return result;
    }
    if (pages.size() > std::numeric_limits<std::uint32_t>::max()) {
        result.diagnostics = "Virtual geometry hierarchy exceeds the 32-bit page contract.";
        return result;
    }

    std::uint64_t expected_meshlet_offset = 0U;
    std::uint64_t expected_vertex_offset = 0U;
    std::uint64_t expected_triangle_offset = 0U;
    for (std::size_t index = 0U; index < pages.size(); ++index) {
        const auto& page = pages[index];
        const bool final_page = index + 1U == pages.size();
        if (page.meshlet_count == 0U ||
            page.meshlet_count > virtual_geometry_page_max_meshlets ||
            (!final_page && page.meshlet_count != virtual_geometry_page_max_meshlets)) {
            result.diagnostics = "Virtual geometry hierarchy input has a non-canonical page meshlet count.";
            return result;
        }
        if (page.vertex_index_count == 0U || page.triangle_index_count == 0U) {
            result.diagnostics = "Virtual geometry hierarchy input contains an empty page stream range.";
            return result;
        }
        if (page.meshlet_offset != expected_meshlet_offset ||
            page.vertex_index_offset != expected_vertex_offset ||
            page.triangle_index_offset != expected_triangle_offset) {
            result.diagnostics = "Virtual geometry hierarchy page ranges must be canonical and contiguous.";
            return result;
        }
        if (!finite_bounds(page.bounds)) {
            result.diagnostics = "Virtual geometry hierarchy input contains invalid page bounds.";
            return result;
        }

        expected_meshlet_offset += page.meshlet_count;
        expected_vertex_offset += page.vertex_index_count;
        expected_triangle_offset += page.triangle_index_count;
        if (expected_meshlet_offset > std::numeric_limits<std::uint32_t>::max() ||
            expected_vertex_offset > std::numeric_limits<std::uint32_t>::max() ||
            expected_triangle_offset > std::numeric_limits<std::uint32_t>::max()) {
            result.diagnostics = "Virtual geometry hierarchy page ranges overflow the 32-bit contract.";
            return result;
        }
    }

    std::vector<std::vector<HierarchyBuildNode>> levels;
    levels.emplace_back();
    levels.back().reserve(pages.size());
    for (std::size_t index = 0U; index < pages.size(); ++index) {
        levels.back().push_back(HierarchyBuildNode{
            .page_offset = static_cast<std::uint32_t>(index),
            .page_count = 1U,
            .bounds = pages[index].bounds,
            .geometric_error = 0.0F,
        });
    }

    std::size_t total_node_count = levels.back().size();
    while (levels.back().size() > 1U) {
        const auto& children = levels.back();
        std::vector<HierarchyBuildNode> parents;
        parents.reserve(
            (children.size() + virtual_geometry_hierarchy_fanout - 1U) /
            virtual_geometry_hierarchy_fanout
        );

        for (std::size_t child_offset = 0U; child_offset < children.size();
             child_offset += virtual_geometry_hierarchy_fanout) {
            const std::size_t child_count = std::min<std::size_t>(
                virtual_geometry_hierarchy_fanout,
                children.size() - child_offset
            );
            const auto child_group = std::span<const HierarchyBuildNode>{
                children.data() + child_offset, child_count
            };

            std::array<MeshletBounds, virtual_geometry_hierarchy_fanout> child_bounds{};
            std::uint64_t page_count = 0U;
            for (std::size_t local = 0U; local < child_count; ++local) {
                child_bounds[local] = child_group[local].bounds;
                page_count += child_group[local].page_count;
            }
            if (page_count == 0U || page_count > std::numeric_limits<std::uint32_t>::max()) {
                result.diagnostics = "Virtual geometry hierarchy parent page range is invalid.";
                return result;
            }

            const auto bounds = aggregate_bounds(std::span<const MeshletBounds>{
                child_bounds.data(), child_count
            });
            if (!bounds.has_value()) {
                result.diagnostics = "Virtual geometry hierarchy parent bounds are not finite.";
                return result;
            }
            const auto error = hierarchy_parent_error(*bounds, child_group);
            if (!error.has_value()) {
                result.diagnostics = "Virtual geometry hierarchy parent error is not finite.";
                return result;
            }

            parents.push_back(HierarchyBuildNode{
                .page_offset = child_group.front().page_offset,
                .page_count = static_cast<std::uint32_t>(page_count),
                .bounds = *bounds,
                .geometric_error = *error,
            });
        }

        if (total_node_count > std::numeric_limits<std::uint32_t>::max() - parents.size()) {
            result.diagnostics = "Virtual geometry hierarchy exceeds the 32-bit node contract.";
            return result;
        }
        total_node_count += parents.size();
        levels.push_back(std::move(parents));
    }

    std::vector<std::size_t> level_offsets(levels.size(), 0U);
    std::size_t flattened_offset = 0U;
    for (std::size_t level = levels.size(); level-- > 0U;) {
        level_offsets[level] = flattened_offset;
        flattened_offset += levels[level].size();
    }
    if (flattened_offset != total_node_count ||
        total_node_count > std::numeric_limits<std::uint32_t>::max()) {
        result.diagnostics = "Virtual geometry hierarchy flattening exceeded the node contract.";
        return result;
    }

    VirtualGeometryHierarchy hierarchy;
    hierarchy.nodes.reserve(total_node_count);
    hierarchy.root_index = 0U;
    for (std::size_t level = levels.size(); level-- > 0U;) {
        const auto& level_nodes = levels[level];
        const std::uint32_t depth = static_cast<std::uint32_t>(levels.size() - 1U - level);
        for (std::size_t index = 0U; index < level_nodes.size(); ++index) {
            const auto& source = level_nodes[index];
            const bool is_root = level + 1U == levels.size();
            const bool is_leaf = level == 0U;

            std::uint32_t parent_index = virtual_geometry_invalid_node;
            if (!is_root) {
                parent_index = static_cast<std::uint32_t>(
                    level_offsets[level + 1U] + index / virtual_geometry_hierarchy_fanout
                );
            }

            std::uint32_t first_child = virtual_geometry_invalid_node;
            std::uint32_t child_count = 0U;
            if (!is_leaf) {
                const std::size_t first_child_index = index * virtual_geometry_hierarchy_fanout;
                const std::size_t available_children = levels[level - 1U].size() - first_child_index;
                child_count = static_cast<std::uint32_t>(std::min<std::size_t>(
                    virtual_geometry_hierarchy_fanout,
                    available_children
                ));
                first_child = static_cast<std::uint32_t>(
                    level_offsets[level - 1U] + first_child_index
                );
            }

            hierarchy.nodes.push_back(VirtualGeometryHierarchyNode{
                .parent_index = parent_index,
                .first_child = first_child,
                .child_count = child_count,
                .page_offset = source.page_offset,
                .page_count = source.page_count,
                .depth = depth,
                .bounds = source.bounds,
                .geometric_error = source.geometric_error,
            });
        }
    }

    if (hierarchy.nodes.empty() || hierarchy.nodes.front().parent_index != virtual_geometry_invalid_node ||
        hierarchy.nodes.front().page_offset != 0U ||
        hierarchy.nodes.front().page_count != pages.size()) {
        result.diagnostics = "Virtual geometry hierarchy root metadata is invalid.";
        return result;
    }

    result.hierarchy = std::move(hierarchy);
    return result;
}

} // namespace vice::render
