#pragma once

#include <cstdint>

namespace vice::runtime {

enum class RuntimeDirectionalShadowMode : std::uint8_t {
    automatic,
    cascaded,
    ray_traced,
};

enum class RuntimeDirectionalShadowPath : std::uint8_t {
    cascaded,
    ray_traced,
};

struct RuntimeDirectionalShadowTelemetry {
    RuntimeDirectionalShadowMode requested_mode = RuntimeDirectionalShadowMode::automatic;
    RuntimeDirectionalShadowPath active_path = RuntimeDirectionalShadowPath::cascaded;
    bool ray_tracing_supported = false;
    bool ray_query_visibility_available = false;

    friend bool operator==(
        const RuntimeDirectionalShadowTelemetry&,
        const RuntimeDirectionalShadowTelemetry&
    ) = default;
};

[[nodiscard]] constexpr RuntimeDirectionalShadowPath resolve_runtime_directional_shadow_path(
    const RuntimeDirectionalShadowMode requested_mode,
    const bool ray_tracing_supported
) noexcept {
    if (requested_mode == RuntimeDirectionalShadowMode::cascaded) {
        return RuntimeDirectionalShadowPath::cascaded;
    }
    if (!ray_tracing_supported) {
        return RuntimeDirectionalShadowPath::cascaded;
    }
    return RuntimeDirectionalShadowPath::ray_traced;
}

[[nodiscard]] constexpr RuntimeDirectionalShadowPath runtime_directional_shadow_active_path(
    const RuntimeDirectionalShadowMode requested_mode,
    const bool ray_tracing_supported,
    const bool ray_query_visibility_available
) noexcept {
    return resolve_runtime_directional_shadow_path(requested_mode, ray_tracing_supported) ==
            RuntimeDirectionalShadowPath::ray_traced &&
            ray_query_visibility_available
        ? RuntimeDirectionalShadowPath::ray_traced
        : RuntimeDirectionalShadowPath::cascaded;
}

[[nodiscard]] constexpr RuntimeDirectionalShadowTelemetry make_runtime_directional_shadow_telemetry(
    const RuntimeDirectionalShadowMode requested_mode,
    const bool ray_tracing_supported,
    const bool ray_query_visibility_available
) noexcept {
    return RuntimeDirectionalShadowTelemetry{
        .requested_mode = requested_mode,
        .active_path = runtime_directional_shadow_active_path(
            requested_mode,
            ray_tracing_supported,
            ray_query_visibility_available
        ),
        .ray_tracing_supported = ray_tracing_supported,
        .ray_query_visibility_available = ray_query_visibility_available,
    };
}

[[nodiscard]] constexpr bool runtime_directional_shadow_requires_depth_prepass(
    const RuntimeDirectionalShadowPath path
) noexcept {
    return path == RuntimeDirectionalShadowPath::ray_traced;
}

} // namespace vice::runtime
