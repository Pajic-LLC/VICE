#pragma once

#include <vice/runtime/ray_diffuse_gi.hpp>

#include <cmath>
#include <cstdint>

namespace vice::runtime {

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

enum class RuntimeRayDiffuseGiFallbackReason : std::uint8_t {
    not_rendered,
    none,
    disabled,
    ray_tracing_unsupported,
    frame_inputs_unavailable,
    environment_unavailable,
    scene_unavailable,
    pipeline_unavailable,
    invalid_settings,
    pipeline_failure,
};

struct RuntimeRayDiffuseGiTelemetry final {
    RuntimeRayDiffuseGiSettings settings{};
    RuntimeRayDiffuseGiFallbackReason fallback_reason =
        RuntimeRayDiffuseGiFallbackReason::not_rendered;
    std::uint64_t frame_index = 0U;
    bool ray_tracing_supported = false;
    bool pipeline_available = false;
    bool scene_available = false;
    bool history_valid = false;
    bool active = false;

    friend bool operator==(const RuntimeRayDiffuseGiTelemetry&, const RuntimeRayDiffuseGiTelemetry&) = default;
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
