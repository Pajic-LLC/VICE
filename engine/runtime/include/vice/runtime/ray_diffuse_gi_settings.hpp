#pragma once

#include <cmath>
#include <cstdint>

namespace vice::runtime {

inline constexpr float runtime_ray_diffuse_gi_default_max_distance = 40.0F;
inline constexpr float runtime_ray_diffuse_gi_default_surface_bias = 0.02F;
inline constexpr std::uint32_t runtime_ray_diffuse_gi_default_spatial_kernel_radius = 2U;
inline constexpr float runtime_ray_diffuse_gi_default_temporal_history_weight = 0.92F;

struct RuntimeRayDiffuseGiSettings final {
    bool enabled = true;
    float max_distance = runtime_ray_diffuse_gi_default_max_distance;
    float surface_bias = runtime_ray_diffuse_gi_default_surface_bias;
    std::uint32_t spatial_kernel_radius = runtime_ray_diffuse_gi_default_spatial_kernel_radius;
    float temporal_history_weight = runtime_ray_diffuse_gi_default_temporal_history_weight;

    friend bool operator==(const RuntimeRayDiffuseGiSettings&, const RuntimeRayDiffuseGiSettings&) = default;
};

[[nodiscard]] inline bool is_valid(const RuntimeRayDiffuseGiSettings& settings) noexcept {
    return std::isfinite(settings.max_distance) && settings.max_distance > 0.0F &&
        std::isfinite(settings.surface_bias) && settings.surface_bias > 0.0F &&
        settings.surface_bias < settings.max_distance &&
        settings.spatial_kernel_radius >= 1U && settings.spatial_kernel_radius <= 4U &&
        std::isfinite(settings.temporal_history_weight) &&
        settings.temporal_history_weight >= 0.0F && settings.temporal_history_weight <= 1.0F;
}

} // namespace vice::runtime
