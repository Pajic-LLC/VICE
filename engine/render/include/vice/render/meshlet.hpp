#pragma once

#include <vice/render/mesh_asset.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace vice::render {

inline constexpr std::uint32_t meshlet_max_vertices = 64U;
inline constexpr std::uint32_t meshlet_max_triangles = 126U;

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

struct MeshletBuildResult {
    std::optional<MeshletGeometry> geometry;
    std::string diagnostics;

    [[nodiscard]] explicit operator bool() const noexcept {
        return geometry.has_value();
    }
};

[[nodiscard]] MeshletBuildResult build_meshlets(
    std::span<const MeshVertex> vertices,
    std::span<const std::uint32_t> indices
);

} // namespace vice::render
