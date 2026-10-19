#pragma once

#include <vice/world/spatial.hpp>

#include <cmath>
#include <cstdint>

namespace vice::world {

struct ReflectionProbe {
    LocalPosition half_extents{5.0F, 5.0F, 5.0F};
    float blend_distance = 1.0F;
    std::int32_t priority = 0;
    bool enabled = true;

    friend bool operator==(const ReflectionProbe&, const ReflectionProbe&) = default;
};

[[nodiscard]] inline bool valid(const ReflectionProbe& probe) noexcept {
    return finite(probe.half_extents) &&
        probe.half_extents.x > 0.0F && probe.half_extents.y > 0.0F && probe.half_extents.z > 0.0F &&
        std::isfinite(probe.blend_distance) && probe.blend_distance >= 0.0F;
}

} // namespace vice::world
