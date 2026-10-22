#include <vice/render/meshlet.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
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

} // namespace vice::render
