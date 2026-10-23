#pragma once

#include <vice/render/mesh_asset.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numbers>
#include <optional>
#include <string>
#include <vector>

namespace vice::render {

struct VirtualGeometryFrustumPlane {
    std::array<float, 3U> normal{};
    float distance = 0.0F;

    friend bool operator==(const VirtualGeometryFrustumPlane&, const VirtualGeometryFrustumPlane&) = default;
};

struct VirtualGeometrySelectionTransform {
    std::array<float, 9U> linear{
        1.0F, 0.0F, 0.0F,
        0.0F, 1.0F, 0.0F,
        0.0F, 0.0F, 1.0F,
    };
    std::array<float, 3U> translation{};

    friend bool operator==(const VirtualGeometrySelectionTransform&, const VirtualGeometrySelectionTransform&) = default;
};

struct VirtualGeometrySelectionView {
    std::array<float, 3U> camera_position{};
    std::array<VirtualGeometryFrustumPlane, 6U> frustum_planes{};
    float vertical_fov_radians = 1.0471975512F;
    float viewport_height_pixels = 1080.0F;
    float max_projected_error_pixels = 1.0F;

    friend bool operator==(const VirtualGeometrySelectionView&, const VirtualGeometrySelectionView&) = default;
};

struct VirtualGeometrySelection {
    std::vector<std::uint32_t> selected_node_indices;
    std::vector<std::uint32_t> authoritative_page_indices;

    friend bool operator==(const VirtualGeometrySelection&, const VirtualGeometrySelection&) = default;
};

struct VirtualGeometrySelectionResult {
    std::optional<VirtualGeometrySelection> selection;
    std::string diagnostics;

    [[nodiscard]] explicit operator bool() const noexcept {
        return selection.has_value();
    }
};

namespace virtual_geometry_selection_detail {

struct TransformedSphere {
    std::array<double, 3U> center{};
    double radius = 0.0;
};

[[nodiscard]] inline bool finite_bounds(const MeshletBounds& bounds) noexcept {
    return std::isfinite(bounds.center[0]) &&
        std::isfinite(bounds.center[1]) &&
        std::isfinite(bounds.center[2]) &&
        std::isfinite(bounds.radius) && bounds.radius >= 0.0F;
}

[[nodiscard]] inline bool contains_bound(
    const MeshletBounds& outer,
    const MeshletBounds& inner
) noexcept {
    if (!finite_bounds(outer) || !finite_bounds(inner)) return false;
    const double dx = static_cast<double>(inner.center[0]) - outer.center[0];
    const double dy = static_cast<double>(inner.center[1]) - outer.center[1];
    const double dz = static_cast<double>(inner.center[2]) - outer.center[2];
    const double center_distance = std::sqrt(dx * dx + dy * dy + dz * dz);
    const double tolerance = std::max(
        1.0e-4,
        static_cast<double>(outer.radius) * 1.0e-5
    );
    return std::isfinite(center_distance) &&
        center_distance + static_cast<double>(inner.radius) <=
            static_cast<double>(outer.radius) + tolerance;
}

[[nodiscard]] inline bool validate_hierarchy(
    const VirtualGeometryHierarchy& hierarchy,
    std::string& diagnostics
) {
    const auto& nodes = hierarchy.nodes;
    if (nodes.empty() || hierarchy.root_index != 0U) {
        diagnostics = "Virtual geometry selection requires a non-empty root-first hierarchy.";
        return false;
    }
    if (nodes.size() > std::numeric_limits<std::uint32_t>::max()) {
        diagnostics = "Virtual geometry selection hierarchy exceeds the 32-bit node contract.";
        return false;
    }

    const auto& root = nodes.front();
    if (root.parent_index != virtual_geometry_invalid_node || root.depth != 0U ||
        root.page_offset != 0U || root.page_count == 0U || root.page_count > nodes.size()) {
        diagnostics = "Virtual geometry selection hierarchy has an invalid root contract.";
        return false;
    }

    std::vector<std::uint32_t> incoming(nodes.size(), 0U);
    std::vector<std::uint32_t> leaf_pages;
    leaf_pages.reserve(root.page_count);

    for (std::size_t index = 0U; index < nodes.size(); ++index) {
        const auto& node = nodes[index];
        if (!finite_bounds(node.bounds) || !std::isfinite(node.geometric_error) ||
            node.geometric_error < 0.0F || node.page_count == 0U) {
            diagnostics = "Virtual geometry selection hierarchy contains invalid node bounds, error, or page count.";
            return false;
        }

        const std::uint64_t page_end = static_cast<std::uint64_t>(node.page_offset) +
            node.page_count;
        if (page_end > root.page_count) {
            diagnostics = "Virtual geometry selection hierarchy contains an out-of-range page span.";
            return false;
        }

        if (index == hierarchy.root_index) {
            if (node.parent_index != virtual_geometry_invalid_node) {
                diagnostics = "Virtual geometry selection root unexpectedly has a parent.";
                return false;
            }
        } else if (node.parent_index == virtual_geometry_invalid_node ||
            node.parent_index >= index || node.parent_index >= nodes.size()) {
            diagnostics = "Virtual geometry selection hierarchy has a non-canonical parent index.";
            return false;
        }

        if (node.child_count == 0U) {
            if (node.first_child != virtual_geometry_invalid_node || node.page_count != 1U) {
                diagnostics = "Virtual geometry selection hierarchy leaf contract is invalid.";
                return false;
            }
            leaf_pages.push_back(node.page_offset);
            continue;
        }

        if (node.child_count > virtual_geometry_hierarchy_fanout ||
            node.first_child == virtual_geometry_invalid_node ||
            node.first_child <= index ||
            static_cast<std::uint64_t>(node.first_child) + node.child_count > nodes.size()) {
            diagnostics = "Virtual geometry selection hierarchy has an invalid child range.";
            return false;
        }

        std::uint64_t expected_page_offset = node.page_offset;
        std::uint64_t child_page_count = 0U;
        for (std::uint32_t local = 0U; local < node.child_count; ++local) {
            const std::uint32_t child_index = node.first_child + local;
            const auto& child = nodes[child_index];
            if (child.parent_index != index || child.depth != node.depth + 1U ||
                child.page_offset != expected_page_offset || !contains_bound(node.bounds, child.bounds)) {
                diagnostics = "Virtual geometry selection hierarchy child connectivity or coverage is invalid.";
                return false;
            }
            const double error_tolerance = std::max(
                1.0e-4,
                static_cast<double>(node.geometric_error) * 1.0e-5
            );
            if (static_cast<double>(child.geometric_error) >
                static_cast<double>(node.geometric_error) + error_tolerance) {
                diagnostics = "Virtual geometry selection hierarchy error is not monotonic.";
                return false;
            }
            ++incoming[child_index];
            expected_page_offset += child.page_count;
            child_page_count += child.page_count;
        }
        if (child_page_count != node.page_count ||
            expected_page_offset != static_cast<std::uint64_t>(node.page_offset) + node.page_count) {
            diagnostics = "Virtual geometry selection hierarchy child page ranges do not exactly cover their parent.";
            return false;
        }
    }

    for (std::size_t index = 1U; index < incoming.size(); ++index) {
        if (incoming[index] != 1U) {
            diagnostics = "Virtual geometry selection hierarchy contains disconnected or multiply referenced nodes.";
            return false;
        }
    }

    if (leaf_pages.size() != root.page_count) {
        diagnostics = "Virtual geometry selection hierarchy leaves do not cover the root page range exactly once.";
        return false;
    }
    std::sort(leaf_pages.begin(), leaf_pages.end());
    for (std::size_t index = 0U; index < leaf_pages.size(); ++index) {
        if (leaf_pages[index] != index) {
            diagnostics = "Virtual geometry selection hierarchy leaf pages are gapped or overlapping.";
            return false;
        }
    }
    return true;
}

[[nodiscard]] inline std::optional<double> transform_radius_scale(
    const VirtualGeometrySelectionTransform& transform
) noexcept {
    double one_norm = 0.0;
    double infinity_norm = 0.0;
    for (std::size_t row = 0U; row < 3U; ++row) {
        double row_sum = 0.0;
        for (std::size_t column = 0U; column < 3U; ++column) {
            const double value = static_cast<double>(transform.linear[row * 3U + column]);
            if (!std::isfinite(value)) return std::nullopt;
            row_sum += std::abs(value);
        }
        infinity_norm = std::max(infinity_norm, row_sum);
    }
    for (std::size_t column = 0U; column < 3U; ++column) {
        double column_sum = 0.0;
        for (std::size_t row = 0U; row < 3U; ++row) {
            column_sum += std::abs(static_cast<double>(transform.linear[row * 3U + column]));
        }
        one_norm = std::max(one_norm, column_sum);
    }
    for (const float value : transform.translation) {
        if (!std::isfinite(value)) return std::nullopt;
    }
    const double scale = std::sqrt(one_norm * infinity_norm);
    if (!std::isfinite(scale)) return std::nullopt;
    return scale;
}

[[nodiscard]] inline std::optional<TransformedSphere> transform_bounds(
    const MeshletBounds& bounds,
    const VirtualGeometrySelectionTransform& transform,
    const double radius_scale
) noexcept {
    if (!finite_bounds(bounds) || !std::isfinite(radius_scale) || radius_scale < 0.0) {
        return std::nullopt;
    }

    TransformedSphere sphere;
    for (std::size_t row = 0U; row < 3U; ++row) {
        double value = static_cast<double>(transform.translation[row]);
        for (std::size_t column = 0U; column < 3U; ++column) {
            value += static_cast<double>(transform.linear[row * 3U + column]) *
                static_cast<double>(bounds.center[column]);
        }
        if (!std::isfinite(value)) return std::nullopt;
        sphere.center[row] = value;
    }
    sphere.radius = static_cast<double>(bounds.radius) * radius_scale;
    if (!std::isfinite(sphere.radius)) return std::nullopt;
    return sphere;
}

[[nodiscard]] inline bool visible(
    const TransformedSphere& sphere,
    const VirtualGeometrySelectionView& view
) noexcept {
    for (const auto& plane : view.frustum_planes) {
        const double signed_distance =
            static_cast<double>(plane.normal[0]) * sphere.center[0] +
            static_cast<double>(plane.normal[1]) * sphere.center[1] +
            static_cast<double>(plane.normal[2]) * sphere.center[2] +
            static_cast<double>(plane.distance);
        if (signed_distance < -sphere.radius) return false;
    }
    return true;
}

[[nodiscard]] inline std::optional<double> pixel_projection_scale(
    const VirtualGeometrySelectionView& view
) noexcept {
    if (!std::isfinite(view.vertical_fov_radians) ||
        view.vertical_fov_radians <= 0.0F ||
        view.vertical_fov_radians >= std::numbers::pi_v<float> ||
        !std::isfinite(view.viewport_height_pixels) || view.viewport_height_pixels <= 0.0F ||
        !std::isfinite(view.max_projected_error_pixels) || view.max_projected_error_pixels < 0.0F) {
        return std::nullopt;
    }
    for (const float value : view.camera_position) {
        if (!std::isfinite(value)) return std::nullopt;
    }
    for (const auto& plane : view.frustum_planes) {
        if (!std::isfinite(plane.normal[0]) || !std::isfinite(plane.normal[1]) ||
            !std::isfinite(plane.normal[2]) || !std::isfinite(plane.distance)) {
            return std::nullopt;
        }
        const double length = std::sqrt(
            static_cast<double>(plane.normal[0]) * plane.normal[0] +
            static_cast<double>(plane.normal[1]) * plane.normal[1] +
            static_cast<double>(plane.normal[2]) * plane.normal[2]
        );
        if (!std::isfinite(length) || std::abs(length - 1.0) > 1.0e-3) {
            return std::nullopt;
        }
    }

    const double tangent = std::tan(static_cast<double>(view.vertical_fov_radians) * 0.5);
    if (!std::isfinite(tangent) || tangent <= 0.0) return std::nullopt;
    const double scale = static_cast<double>(view.viewport_height_pixels) / (2.0 * tangent);
    if (!std::isfinite(scale) || scale <= 0.0) return std::nullopt;
    return scale;
}

[[nodiscard]] inline double projected_error_pixels(
    const VirtualGeometryHierarchyNode& node,
    const TransformedSphere& sphere,
    const VirtualGeometrySelectionView& view,
    const double radius_scale,
    const double projection_scale
) noexcept {
    const double geometric_error = static_cast<double>(node.geometric_error) * radius_scale;
    if (geometric_error <= 0.0) return 0.0;

    const double dx = sphere.center[0] - static_cast<double>(view.camera_position[0]);
    const double dy = sphere.center[1] - static_cast<double>(view.camera_position[1]);
    const double dz = sphere.center[2] - static_cast<double>(view.camera_position[2]);
    const double center_distance = std::sqrt(dx * dx + dy * dy + dz * dz);
    const double surface_distance = center_distance - sphere.radius;
    if (!std::isfinite(surface_distance) || surface_distance <= 1.0e-6) {
        return std::numeric_limits<double>::infinity();
    }
    return geometric_error * projection_scale / surface_distance;
}

inline void collect_visible_leaf_pages(
    const VirtualGeometryHierarchy& hierarchy,
    const std::uint32_t node_index,
    const VirtualGeometrySelectionView& view,
    const VirtualGeometrySelectionTransform& transform,
    const double radius_scale,
    std::vector<std::uint32_t>& pages
) {
    const auto& node = hierarchy.nodes[node_index];
    const auto sphere = transform_bounds(node.bounds, transform, radius_scale);
    if (!sphere.has_value() || !visible(*sphere, view)) return;
    if (node.child_count == 0U) {
        pages.push_back(node.page_offset);
        return;
    }
    for (std::uint32_t local = 0U; local < node.child_count; ++local) {
        collect_visible_leaf_pages(
            hierarchy, node.first_child + local, view, transform, radius_scale, pages
        );
    }
}

inline void select_node(
    const VirtualGeometryHierarchy& hierarchy,
    const std::uint32_t node_index,
    const VirtualGeometrySelectionView& view,
    const VirtualGeometrySelectionTransform& transform,
    const double radius_scale,
    const double projection_scale,
    VirtualGeometrySelection& selection
) {
    const auto& node = hierarchy.nodes[node_index];
    const auto sphere = transform_bounds(node.bounds, transform, radius_scale);
    if (!sphere.has_value() || !visible(*sphere, view)) return;

    if (node.child_count == 0U) {
        selection.selected_node_indices.push_back(node_index);
        selection.authoritative_page_indices.push_back(node.page_offset);
        return;
    }

    const double projected_error = projected_error_pixels(
        node, *sphere, view, radius_scale, projection_scale
    );
    if (projected_error <= static_cast<double>(view.max_projected_error_pixels)) {
        std::vector<std::uint32_t> fallback_pages;
        fallback_pages.reserve(node.page_count);
        for (std::uint32_t local = 0U; local < node.child_count; ++local) {
            collect_visible_leaf_pages(
                hierarchy,
                node.first_child + local,
                view,
                transform,
                radius_scale,
                fallback_pages
            );
        }
        if (!fallback_pages.empty()) {
            selection.selected_node_indices.push_back(node_index);
            selection.authoritative_page_indices.insert(
                selection.authoritative_page_indices.end(),
                fallback_pages.begin(),
                fallback_pages.end()
            );
        }
        return;
    }

    for (std::uint32_t local = 0U; local < node.child_count; ++local) {
        select_node(
            hierarchy,
            node.first_child + local,
            view,
            transform,
            radius_scale,
            projection_scale,
            selection
        );
    }
}

} // namespace virtual_geometry_selection_detail

// Selection inputs live in one camera-relative coordinate space. The hierarchy remains
// asset-local; transform maps its bounds into that camera-relative space before culling
// and projected-error evaluation. Frustum planes use dot(normal, point) + distance >= 0
// for the inside half-space and must have unit-length normals.
[[nodiscard]] inline VirtualGeometrySelectionResult select_virtual_geometry(
    const VirtualGeometryHierarchy& hierarchy,
    const VirtualGeometrySelectionView& view,
    const VirtualGeometrySelectionTransform& transform = {}
) {
    using namespace virtual_geometry_selection_detail;

    VirtualGeometrySelectionResult result;
    if (!validate_hierarchy(hierarchy, result.diagnostics)) return result;

    const auto radius_scale = transform_radius_scale(transform);
    if (!radius_scale.has_value()) {
        result.diagnostics = "Virtual geometry selection transform is non-finite.";
        return result;
    }
    const auto projection_scale = pixel_projection_scale(view);
    if (!projection_scale.has_value()) {
        result.diagnostics = "Virtual geometry selection view parameters are invalid.";
        return result;
    }

    VirtualGeometrySelection selection;
    selection.selected_node_indices.reserve(hierarchy.nodes.size());
    selection.authoritative_page_indices.reserve(hierarchy.nodes.front().page_count);
    select_node(
        hierarchy,
        hierarchy.root_index,
        view,
        transform,
        *radius_scale,
        *projection_scale,
        selection
    );

    for (std::size_t index = 1U; index < selection.authoritative_page_indices.size(); ++index) {
        if (selection.authoritative_page_indices[index - 1U] >=
            selection.authoritative_page_indices[index]) {
            result.diagnostics = "Virtual geometry selection produced non-canonical fallback page ordering.";
            return result;
        }
    }

    result.selection = std::move(selection);
    return result;
}

} // namespace vice::render
