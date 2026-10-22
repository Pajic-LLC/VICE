#pragma once

#include <vice/runtime/post_process.hpp>
#include <vice/runtime/scene.hpp>

#include <cmath>
#include <span>
#include <unordered_map>

namespace vice::runtime::detail {

struct RuntimeHiZCameraSnapshot {
    world::PersistentId entity{};
    world::GlobalPosition global_position{};
    math::Vec3 forward{0.0F, 0.0F, -1.0F};
    math::Vec3 up{0.0F, 1.0F, 0.0F};
    math::Mat4 camera_relative_view_projection{};
    float vertical_fov_radians = 0.0F;
    float aspect_ratio = 0.0F;
    float near_plane = 0.0F;
    float far_plane = 0.0F;
    bool valid = false;
};

struct RuntimeHiZInstanceSnapshot {
    world::ResourceId mesh{};
    std::optional<world::ResourceId> material;
    world::SpatialState spatial{};
    world::AuthoredTransform authored{};

    friend bool operator==(const RuntimeHiZInstanceSnapshot&, const RuntimeHiZInstanceSnapshot&) = default;
};

class RuntimeHiZHistory final {
public:
    [[nodiscard]] bool usable(
        const world::PersistentId camera_entity,
        const world::GlobalPosition camera_global_position,
        const math::Vec3 camera_forward,
        const math::Vec3 camera_up,
        const world::Camera& camera,
        const float aspect_ratio,
        const std::span<const RenderMeshInstance> instances
    ) const noexcept {
        if (!camera_.valid || camera_.entity != camera_entity ||
            !world::finite(camera_global_position) || !std::isfinite(aspect_ratio) ||
            aspect_ratio <= 0.0F || !std::isfinite(camera.vertical_fov_radians) ||
            !std::isfinite(camera.near_plane) || !std::isfinite(camera.far_plane)) {
            return false;
        }

        const double dx = camera_global_position.x - camera_.global_position.x;
        const double dy = camera_global_position.y - camera_.global_position.y;
        const double dz = camera_global_position.z - camera_.global_position.z;
        const double distance = std::hypot(std::hypot(dx, dy), dz);
        if (!std::isfinite(distance) ||
            distance > static_cast<double>(runtime_taa_camera_cut_distance)) {
            return false;
        }

        const auto current_forward = math::normalize(camera_forward);
        const auto current_up = math::normalize(camera_up);
        if (math::dot(current_forward, camera_.forward) < runtime_taa_camera_cut_forward_dot ||
            math::dot(current_up, camera_.up) < runtime_taa_camera_cut_forward_dot) {
            return false;
        }

        constexpr float projection_epsilon = 0.00001F;
        if (std::abs(camera.vertical_fov_radians - camera_.vertical_fov_radians) > projection_epsilon ||
            std::abs(aspect_ratio - camera_.aspect_ratio) > projection_epsilon ||
            std::abs(camera.near_plane - camera_.near_plane) > projection_epsilon ||
            std::abs(camera.far_plane - camera_.far_plane) > projection_epsilon) {
            return false;
        }

        if (instances.size() != instances_.size()) return false;
        for (const auto& instance : instances) {
            const auto found = instances_.find(instance.entity);
            if (found == instances_.end()) return false;
            const RuntimeHiZInstanceSnapshot current{
                .mesh = instance.mesh,
                .material = instance.material,
                .spatial = instance.spatial,
                .authored = instance.authored,
            };
            if (found->second != current) return false;
        }
        return true;
    }

    void commit(
        const world::PersistentId camera_entity,
        const world::GlobalPosition camera_global_position,
        const math::Vec3 camera_forward,
        const math::Vec3 camera_up,
        const world::Camera& camera,
        const float aspect_ratio,
        const math::Mat4& camera_relative_view_projection,
        const std::span<const RenderMeshInstance> instances
    ) {
        camera_ = RuntimeHiZCameraSnapshot{
            .entity = camera_entity,
            .global_position = camera_global_position,
            .forward = math::normalize(camera_forward),
            .up = math::normalize(camera_up),
            .camera_relative_view_projection = camera_relative_view_projection,
            .vertical_fov_radians = camera.vertical_fov_radians,
            .aspect_ratio = aspect_ratio,
            .near_plane = camera.near_plane,
            .far_plane = camera.far_plane,
            .valid = true,
        };
        instances_.clear();
        instances_.reserve(instances.size());
        for (const auto& instance : instances) {
            instances_.insert_or_assign(
                instance.entity,
                RuntimeHiZInstanceSnapshot{
                    .mesh = instance.mesh,
                    .material = instance.material,
                    .spatial = instance.spatial,
                    .authored = instance.authored,
                }
            );
        }
    }

    void reset() noexcept {
        camera_ = {};
        instances_.clear();
    }

    [[nodiscard]] const RuntimeHiZCameraSnapshot& camera() const noexcept {
        return camera_;
    }

private:
    RuntimeHiZCameraSnapshot camera_{};
    std::unordered_map<
        world::PersistentId,
        RuntimeHiZInstanceSnapshot,
        world::PersistentIdHash
    > instances_;
};

} // namespace vice::runtime::detail
