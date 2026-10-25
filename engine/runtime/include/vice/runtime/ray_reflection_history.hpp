#pragma once

#include <vice/runtime/ray_hit_radiance_history.hpp>

namespace vice::runtime {

inline constexpr float runtime_ray_reflection_history_depth_threshold =
    runtime_ray_hit_radiance_history_depth_threshold;

using RuntimeRayReflectionHistoryProjection = RuntimeRayHitRadianceHistoryProjection;
using RuntimeRayReflectionHistoryConstants = RuntimeRayHitRadianceHistoryConstants;
using RuntimeRayReflectionHistoryPass = RuntimeRayHitRadianceHistoryPass;

[[nodiscard]] inline std::optional<RuntimeRayReflectionHistoryProjection>
runtime_ray_reflection_history_projection(
    const math::Mat4& previous_view_projection,
    const math::Vec3 world_position
) noexcept {
    return runtime_ray_hit_radiance_history_projection(
        previous_view_projection,
        world_position
    );
}

[[nodiscard]] inline bool runtime_ray_reflection_history_depth_matches(
    const float expected_device_depth,
    const float sampled_device_depth,
    const float depth_threshold = runtime_ray_reflection_history_depth_threshold
) noexcept {
    return runtime_ray_hit_radiance_history_depth_matches(
        expected_device_depth,
        sampled_device_depth,
        depth_threshold
    );
}

[[nodiscard]] inline std::optional<math::Vec3>
runtime_ray_reflection_history_rebase_position(
    const math::Vec3 position,
    const world::GlobalPosition current_origin,
    const world::GlobalPosition previous_origin
) noexcept {
    return runtime_ray_hit_radiance_history_rebase_position(
        position,
        current_origin,
        previous_origin
    );
}

[[nodiscard]] inline std::optional<RuntimeRayReflectionHistoryConstants>
make_runtime_ray_reflection_history_constants(
    const math::Mat4& previous_view_projection,
    const std::uint32_t width,
    const std::uint32_t height,
    const bool history_valid,
    const float depth_threshold = runtime_ray_reflection_history_depth_threshold
) noexcept {
    return make_runtime_ray_hit_radiance_history_constants(
        previous_view_projection,
        width,
        height,
        history_valid,
        depth_threshold
    );
}

[[nodiscard]] inline std::optional<RuntimeRayReflectionHistoryConstants>
make_runtime_ray_reflection_history_constants(
    const math::Mat4& previous_view_projection,
    const std::uint32_t width,
    const std::uint32_t height,
    const bool history_valid,
    const world::GlobalPosition current_origin,
    const world::GlobalPosition previous_origin,
    const float depth_threshold = runtime_ray_reflection_history_depth_threshold
) noexcept {
    return make_runtime_ray_hit_radiance_history_constants(
        previous_view_projection,
        width,
        height,
        history_valid,
        current_origin,
        previous_origin,
        depth_threshold
    );
}

} // namespace vice::runtime
