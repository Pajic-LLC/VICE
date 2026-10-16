#pragma once

#include <vice/math/math.hpp>
#include <vice/rigid_body/collision_asset.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace vice::tools::collision_import {

enum class CollisionRepresentation : std::uint8_t {
    none,
    triangle_mesh,
    convex_hull,
};

struct SourceTransform {
    math::Vec3 position{};
    math::Quat orientation{};
    math::Vec3 scale{1.0F, 1.0F, 1.0F};
};

struct CollisionAuthoring {
    CollisionRepresentation representation = CollisionRepresentation::none;
    std::optional<rigid_body::ShapeId> shape_id;
};

struct ImportedMesh {
    std::string source_key;
    std::vector<math::Vec3> positions;
    std::vector<std::uint32_t> indices;
    SourceTransform local_transform{};
    bool render_mesh = true;
    CollisionAuthoring collision{};
};

struct ShapeBinding {
    std::string source_key;
    rigid_body::ShapeId shape{};
    SourceTransform source_transform{};
    CollisionRepresentation representation = CollisionRepresentation::none;
    bool render_mesh = true;
};

struct BuildResult {
    std::optional<std::vector<rigid_body::CollisionAssetShape>> shapes;
    std::vector<ShapeBinding> bindings;
    std::string diagnostics;
};

struct CookResult {
    bool success = false;
    std::uint32_t asset_version = 0;
    std::uint64_t source_fingerprint = 0;
    std::vector<std::byte> bytes;
    std::vector<ShapeBinding> bindings;
    std::string diagnostics;
};

[[nodiscard]] BuildResult build_collision_shapes(
    std::span<const ImportedMesh> meshes
);

[[nodiscard]] CookResult cook_collision(
    std::span<const ImportedMesh> meshes
);

} // namespace vice::tools::collision_import
