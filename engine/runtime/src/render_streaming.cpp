#include <vice/runtime/render_streaming.hpp>

#include <vice/math/math.hpp>

#include <optional>
#include <utility>

namespace vice::runtime {
namespace {

[[nodiscard]] math::Vec3 add(const math::Vec3 left, const math::Vec3 right) noexcept {
    return {left.x + right.x, left.y + right.y, left.z + right.z};
}

[[nodiscard]] math::Vec3 scaled(const math::Vec3 value, const float scale) noexcept {
    return {value.x * scale, value.y * scale, value.z * scale};
}

[[nodiscard]] math::Vec3 rotate_vector(
    const world::AuthoredRotation rotation,
    const math::Vec3 value
) noexcept {
    const math::Vec3 axis{rotation.x, rotation.y, rotation.z};
    const float axis_dot = math::dot(axis, value);
    const float axis_length_squared = math::dot(axis, axis);
    return add(
        add(
            scaled(axis, 2.0F * axis_dot),
            scaled(value, rotation.w * rotation.w - axis_length_squared)
        ),
        scaled(math::cross(axis, value), 2.0F * rotation.w)
    );
}

} // namespace

RuntimeRenderResidentSceneBuildResult build_runtime_render_resident_scene(
    const world::World& world
) {
    RuntimeRenderResidentSceneBuildResult result;
    result.scene.snapshot = world.snapshot();
    auto residency_build = build_runtime_render_residency(result.scene.snapshot);
    if (!residency_build) {
        result.diagnostics = std::move(residency_build.diagnostics);
        return result;
    }
    result.scene.residency = std::move(residency_build.plan);

    for (const auto& entity : result.scene.snapshot.entities) {
        if (!entity.spatial_state.has_value() ||
            !result.scene.residency.resident(entity.spatial_state->region)) {
            continue;
        }

        std::optional<world::ResourceId> mesh;
        std::optional<world::ResourceId> material;
        for (const auto& reference : entity.resource_references) {
            if (!reference.resource.valid()) continue;
            if (reference.role == world::ResourceRole::render_mesh) mesh = reference.resource;
            if (reference.role == world::ResourceRole::render_material) material = reference.resource;
        }
        if (mesh.has_value()) {
            result.scene.meshes.push_back(RenderMeshInstance{
                .entity = entity.persistent_id,
                .mesh = *mesh,
                .material = material,
                .spatial = *entity.spatial_state,
                .authored = entity.authored_transform.value_or(world::AuthoredTransform{}),
            });
        }

        bool supplied = true;
        if (entity.light && entity.light->enabled && entity.dependency) {
            const auto owner = world.find(entity.persistent_id);
            supplied = owner && world.dependency_available(*owner);
        }
        if (entity.light.has_value() && entity.light->enabled && supplied &&
            result.scene.lights.size() < max_runtime_lights) {
            const auto authored = entity.authored_transform.value_or(world::AuthoredTransform{});
            RuntimeLight light{
                .entity = entity.persistent_id,
                .light = *entity.light,
                .position = {
                    static_cast<float>(entity.spatial_state->global_position.x),
                    static_cast<float>(entity.spatial_state->global_position.y),
                    static_cast<float>(entity.spatial_state->global_position.z),
                },
                .global_position = entity.spatial_state->global_position,
            };
            if (light.light.type == world::LightType::directional) {
                light.direction = math::normalize(
                    rotate_vector(authored.rotation, {0.0F, 0.0F, -1.0F})
                );
            }
            result.scene.lights.push_back(light);
        }

        if (entity.reflection_probe.has_value() && entity.reflection_probe->enabled) {
            std::optional<world::ResourceId> environment;
            for (const auto& reference : entity.resource_references) {
                if (reference.role == world::ResourceRole::reflection_environment &&
                    reference.resource.valid()) {
                    environment = reference.resource;
                    break;
                }
            }
            if (environment.has_value()) {
                result.scene.reflection_probes.push_back(RuntimeReflectionProbe{
                    .entity = entity.persistent_id,
                    .probe = *entity.reflection_probe,
                    .environment = *environment,
                    .position = entity.spatial_state->global_position,
                    .authored = entity.authored_transform.value_or(world::AuthoredTransform{}),
                });
            }
        }
    }

    return result;
}

} // namespace vice::runtime
