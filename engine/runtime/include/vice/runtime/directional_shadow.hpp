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

} // namespace vice::runtime
