#pragma once

#include <vice/render/mesh_asset.hpp>

#include <array>
#include <cstdint>
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

// Selection inputs live in one camera-relative coordinate space. The hierarchy remains
// asset-local; transform maps its bounds into that camera-relative space before culling
// and projected-error evaluation. Frustum planes use dot(normal, point) + distance >= 0
// for the inside half-space and must have unit-length normals.
[[nodiscard]] VirtualGeometrySelectionResult select_virtual_geometry(
    const VirtualGeometryHierarchy& hierarchy,
    const VirtualGeometrySelectionView& view,
    const VirtualGeometrySelectionTransform& transform = {}
);

} // namespace vice::render
