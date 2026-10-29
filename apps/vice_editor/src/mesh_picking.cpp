#include <vice/editor/mesh_picking.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace vice::editor {
namespace {

[[nodiscard]] constexpr math::Vec3 add(const math::Vec3 left, const math::Vec3 right) noexcept {
    return {left.x + right.x, left.y + right.y, left.z + right.z};
}

[[nodiscard]] constexpr math::Vec3 scaled(const math::Vec3 value, const float scale) noexcept {
    return {value.x * scale, value.y * scale, value.z * scale};
}

[[nodiscard]] bool finite(const math::Vec3 value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

[[nodiscard]] math::Quat normalized(const world::AuthoredRotation rotation) noexcept {
    const float length_squared = rotation.x * rotation.x + rotation.y * rotation.y +
        rotation.z * rotation.z + rotation.w * rotation.w;
    if (!std::isfinite(length_squared) || length_squared <= 1.0e-12F) return {};
    const float inverse_length = 1.0F / std::sqrt(length_squared);
    return {
        rotation.x * inverse_length,
        rotation.y * inverse_length,
        rotation.z * inverse_length,
        rotation.w * inverse_length,
    };
}

[[nodiscard]] math::Vec3 rotate(const math::Quat quaternion, const math::Vec3 value) noexcept {
    const math::Vec3 vector{quaternion.x, quaternion.y, quaternion.z};
    const math::Vec3 first = math::cross(vector, value);
    const math::Vec3 second = math::cross(vector, first);
    return add(value, add(scaled(first, 2.0F * quaternion.w), scaled(second, 2.0F)));
}

[[nodiscard]] std::optional<float> ray_aabb_distance(
    const math::Vec3 origin,
    const math::Vec3 direction,
    const MeshBounds bounds
) noexcept {
    float near_distance = 0.0F;
    float far_distance = std::numeric_limits<float>::max();

    const auto axis = [](const math::Vec3 value, const int index) noexcept {
        if (index == 0) return value.x;
        if (index == 1) return value.y;
        return value.z;
    };

    for (int index = 0; index < 3; ++index) {
        const float ray_origin = axis(origin, index);
        const float ray_direction = axis(direction, index);
        const float minimum = axis(bounds.minimum, index);
        const float maximum = axis(bounds.maximum, index);

        if (std::abs(ray_direction) < 1.0e-7F) {
            if (ray_origin < minimum || ray_origin > maximum) return std::nullopt;
            continue;
        }

        float first = (minimum - ray_origin) / ray_direction;
        float second = (maximum - ray_origin) / ray_direction;
        if (first > second) std::swap(first, second);
        near_distance = std::max(near_distance, first);
        far_distance = std::min(far_distance, second);
        if (far_distance < near_distance) return std::nullopt;
    }

    return far_distance >= 0.0F
        ? std::optional<float>{std::max(0.0F, near_distance)}
        : std::nullopt;
}

[[nodiscard]] std::optional<std::pair<math::Vec3, math::Vec3>> local_ray(
    const math::Vec3 origin,
    const math::Vec3 direction,
    const world::SpatialState& spatial,
    const std::optional<world::AuthoredTransform>& authored
) noexcept {
    const world::AuthoredTransform transform = authored.value_or(world::AuthoredTransform{});
    if (!std::isfinite(transform.scale.x) || !std::isfinite(transform.scale.y) ||
        !std::isfinite(transform.scale.z) || std::abs(transform.scale.x) <= 1.0e-7F ||
        std::abs(transform.scale.y) <= 1.0e-7F || std::abs(transform.scale.z) <= 1.0e-7F) {
        return std::nullopt;
    }

    const math::Vec3 translation{
        static_cast<float>(spatial.global_position.x),
        static_cast<float>(spatial.global_position.y),
        static_cast<float>(spatial.global_position.z),
    };
    if (!finite(translation)) return std::nullopt;

    const math::Quat orientation = normalized(transform.rotation);
    const math::Quat inverse_orientation{
        -orientation.x,
        -orientation.y,
        -orientation.z,
        orientation.w,
    };

    const math::Vec3 rotated_origin = rotate(inverse_orientation, origin - translation);
    const math::Vec3 rotated_direction = rotate(inverse_orientation, direction);
    const math::Vec3 local_origin{
        rotated_origin.x / transform.scale.x,
        rotated_origin.y / transform.scale.y,
        rotated_origin.z / transform.scale.z,
    };
    const math::Vec3 local_direction{
        rotated_direction.x / transform.scale.x,
        rotated_direction.y / transform.scale.y,
        rotated_direction.z / transform.scale.z,
    };
    if (!finite(local_origin) || !finite(local_direction)) return std::nullopt;
    return std::pair{local_origin, local_direction};
}

[[nodiscard]] std::optional<world::ResourceId> render_mesh_resource(
    const world::SnapshotEntity& entity
) noexcept {
    for (const auto reference : entity.resource_references) {
        if (reference.role == world::ResourceRole::render_mesh) return reference.resource;
    }
    return std::nullopt;
}

} // namespace

std::optional<MeshPickResult>
pick_render_mesh(const world::World &world, const AssetIndex &assets, const math::Vec3 ray_origin,
                 const math::Vec3 ray_direction,
                 const std::span<const MeshPickGeometry> geometry) noexcept {
    if (!finite(ray_origin) || !finite(ray_direction) || math::dot(ray_direction, ray_direction) <= 1.0e-12F) {
        return std::nullopt;
    }

    const math::Vec3 direction = math::normalize(ray_direction);
    float nearest = std::numeric_limits<float>::max();
    std::optional<MeshPickResult> result;

    for (const auto& entity : world.snapshot(world::ComponentPayloadSnapshot::render).entities) {
        if (!entity.spatial_state.has_value()) continue;
        const auto resource = render_mesh_resource(entity);
        if (!resource.has_value()) continue;

        const AssetRecord* asset = assets.find_resource(*resource);
        if (asset == nullptr || !asset->instantiable_mesh() || !asset->mesh_bounds.has_value()) continue;

        const auto override = std::find_if(geometry.begin(), geometry.end(), [&](const auto &g) {
            return g.entity == entity.persistent_id;
        });
        const auto transformed =
            local_ray(ray_origin, direction,
                      override == geometry.end() ? *entity.spatial_state : override->spatial,
                      override == geometry.end() ? entity.authored_transform : override->authored);
        if (!transformed.has_value()) continue;

        const auto distance =
            ray_aabb_distance(transformed->first, transformed->second,
                              override == geometry.end() ? *asset->mesh_bounds : override->bounds);
        if (!distance.has_value() || *distance >= nearest) continue;

        nearest = *distance;
        result = MeshPickResult{
            .persistent_id = entity.persistent_id,
            .resource = *resource,
            .distance = *distance,
        };
    }

    return result;
}

} // namespace vice::editor
