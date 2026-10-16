#include <vice/tools/collision_import.hpp>
#include <vice/tools/collision_cooker.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace vice::tools::collision_import {
namespace {

constexpr std::uint64_t fnv_offset_basis = 14'695'981'039'346'656'037ULL;
constexpr std::uint64_t fnv_prime = 1'099'511'628'211ULL;

[[nodiscard]] bool finite(const math::Vec3 value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

[[nodiscard]] bool finite(const math::Quat value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) &&
        std::isfinite(value.z) && std::isfinite(value.w);
}

[[nodiscard]] float quaternion_length_squared(const math::Quat value) noexcept {
    return value.x * value.x + value.y * value.y + value.z * value.z + value.w * value.w;
}

[[nodiscard]] math::Quat normalized(const math::Quat value) noexcept {
    const float inverse_length = 1.0F / std::sqrt(quaternion_length_squared(value));
    return math::Quat{
        value.x * inverse_length,
        value.y * inverse_length,
        value.z * inverse_length,
        value.w * inverse_length,
    };
}

[[nodiscard]] math::Vec3 add(const math::Vec3 left, const math::Vec3 right) noexcept {
    return math::Vec3{left.x + right.x, left.y + right.y, left.z + right.z};
}

[[nodiscard]] math::Vec3 multiply_components(
    const math::Vec3 left,
    const math::Vec3 right
) noexcept {
    return math::Vec3{left.x * right.x, left.y * right.y, left.z * right.z};
}

[[nodiscard]] math::Vec3 multiply(const math::Vec3 value, const float scalar) noexcept {
    return math::Vec3{value.x * scalar, value.y * scalar, value.z * scalar};
}

[[nodiscard]] math::Vec3 rotate(const math::Vec3 value, const math::Quat orientation) noexcept {
    const math::Vec3 vector_part{orientation.x, orientation.y, orientation.z};
    const math::Vec3 twice_cross = multiply(math::cross(vector_part, value), 2.0F);
    return add(
        value,
        add(
            multiply(twice_cross, orientation.w),
            math::cross(vector_part, twice_cross)
        )
    );
}

[[nodiscard]] bool valid_transform(const SourceTransform& transform) noexcept {
    return finite(transform.position) && finite(transform.orientation) && finite(transform.scale) &&
        quaternion_length_squared(transform.orientation) > 0.000001F &&
        std::abs(transform.scale.x) > 0.000001F &&
        std::abs(transform.scale.y) > 0.000001F &&
        std::abs(transform.scale.z) > 0.000001F;
}

[[nodiscard]] math::Vec3 transform_point(
    const math::Vec3 point,
    const SourceTransform& transform
) noexcept {
    const math::Vec3 scaled = multiply_components(point, transform.scale);
    return add(rotate(scaled, normalized(transform.orientation)), transform.position);
}

[[nodiscard]] std::string_view representation_name(
    const CollisionRepresentation representation
) noexcept {
    switch (representation) {
    case CollisionRepresentation::none:
        return "none";
    case CollisionRepresentation::triangle_mesh:
        return "triangle_mesh";
    case CollisionRepresentation::convex_hull:
        return "convex_hull";
    }
    return "unknown";
}

[[nodiscard]] rigid_body::ShapeId derived_shape_id(
    const std::string_view source_key,
    const CollisionRepresentation representation
) noexcept {
    std::uint64_t hash = fnv_offset_basis;
    const auto append = [&](const std::string_view text) {
        for (const char character : text) {
            hash ^= static_cast<std::uint8_t>(character);
            hash *= fnv_prime;
        }
    };

    append("vice-collision-import:");
    append(source_key);
    append(":");
    append(representation_name(representation));
    if (hash == 0U) {
        hash = 1U;
    }
    return rigid_body::ShapeId{hash};
}

[[nodiscard]] std::string validate_source_mesh(const ImportedMesh& mesh) {
    if (mesh.source_key.empty()) {
        return "Collision source key cannot be empty.";
    }
    if (!valid_transform(mesh.local_transform)) {
        return "Collision source transform is invalid.";
    }
    if (!std::all_of(
            mesh.positions.begin(),
            mesh.positions.end(),
            [](const math::Vec3 point) { return finite(point); }
        )) {
        return "Collision source contains a non-finite position.";
    }

    if (mesh.collision.representation == CollisionRepresentation::triangle_mesh) {
        if (mesh.positions.size() < 3U || mesh.indices.size() < 3U ||
            mesh.indices.size() % 3U != 0U) {
            return "Triangle collision source requires vertices and triangle indices.";
        }
        for (const std::uint32_t index : mesh.indices) {
            if (index >= mesh.positions.size()) {
                return "Triangle collision source contains an out-of-range index.";
            }
        }
    } else if (mesh.collision.representation == CollisionRepresentation::convex_hull) {
        if (mesh.positions.size() < 4U) {
            return "Convex collision source requires at least four positions.";
        }
    }

    return {};
}

} // namespace

BuildResult build_collision_shapes(const std::span<const ImportedMesh> meshes) {
    BuildResult result;
    std::vector<rigid_body::CollisionAssetShape> shapes;
    std::unordered_set<std::string> collision_source_keys;
    std::unordered_set<std::uint64_t> shape_ids;

    for (const auto& mesh : meshes) {
        if (mesh.collision.representation == CollisionRepresentation::none) {
            continue;
        }

        const std::string validation = validate_source_mesh(mesh);
        if (!validation.empty()) {
            result.diagnostics = mesh.source_key.empty()
                ? validation
                : mesh.source_key + ": " + validation;
            return result;
        }
        if (!collision_source_keys.insert(mesh.source_key).second) {
            result.diagnostics =
                "Collision source key is duplicated: " + mesh.source_key + ".";
            return result;
        }

        const rigid_body::ShapeId shape_id = mesh.collision.shape_id.value_or(
            derived_shape_id(mesh.source_key, mesh.collision.representation)
        );
        if (!shape_id || !shape_ids.insert(shape_id.value).second) {
            result.diagnostics =
                "Collision shape ID is zero or duplicated for source: " + mesh.source_key + ".";
            return result;
        }

        rigid_body::CollisionAssetShape shape;
        shape.id = shape_id;

        if (mesh.collision.representation == CollisionRepresentation::triangle_mesh) {
            rigid_body::TriangleMeshShapeDesc desc;
            desc.vertices.reserve(mesh.positions.size());
            for (const auto point : mesh.positions) {
                desc.vertices.push_back(transform_point(point, mesh.local_transform));
            }
            desc.indices = mesh.indices;

            const float determinant = mesh.local_transform.scale.x *
                mesh.local_transform.scale.y * mesh.local_transform.scale.z;
            if (determinant < 0.0F) {
                for (std::size_t index = 0; index < desc.indices.size(); index += 3U) {
                    std::swap(desc.indices[index + 1U], desc.indices[index + 2U]);
                }
            }
            shape.desc = std::move(desc);
        } else if (mesh.collision.representation == CollisionRepresentation::convex_hull) {
            rigid_body::ConvexHullShapeDesc desc;
            desc.points.reserve(mesh.positions.size());
            for (const auto point : mesh.positions) {
                desc.points.push_back(transform_point(point, mesh.local_transform));
            }
            shape.desc = std::move(desc);
        }

        shapes.push_back(std::move(shape));
        result.bindings.push_back(ShapeBinding{
            .source_key = mesh.source_key,
            .shape = shape_id,
            .source_transform = mesh.local_transform,
            .representation = mesh.collision.representation,
            .render_mesh = mesh.render_mesh,
        });
    }

    result.shapes = std::move(shapes);
    return result;
}

CookResult cook_collision(const std::span<const ImportedMesh> meshes) {
    CookResult result;
    BuildResult built = build_collision_shapes(meshes);
    if (!built.shapes.has_value()) {
        result.diagnostics = std::move(built.diagnostics);
        return result;
    }
    if (built.shapes->empty()) {
        result.diagnostics = "Imported scene contains no collision-enabled meshes.";
        return result;
    }

    const auto cooked = collision_cooker::cook(*built.shapes);
    if (!cooked.success) {
        result.diagnostics = cooked.diagnostics;
        return result;
    }

    result.success = true;
    result.source_fingerprint = cooked.source_fingerprint;
    result.bytes = cooked.bytes;
    result.bindings = std::move(built.bindings);

    const auto decoded = rigid_body::decode_collision_asset(result.bytes);
    if (!decoded.asset.has_value()) {
        result.success = false;
        result.bytes.clear();
        result.bindings.clear();
        result.diagnostics = "Cooked collision asset failed runtime decode: " + decoded.diagnostics;
        return result;
    }
    result.asset_version = decoded.asset->version;
    return result;
}

} // namespace vice::tools::collision_import
