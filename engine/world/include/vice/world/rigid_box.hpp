#pragma once

#include <vice/math/math.hpp>

#include <cmath>
#include <cstdint>

namespace vice::world {

enum class RigidMotion : std::uint8_t { static_body, kinematic_body, dynamic_body };

// VICE-owned authoring/state data. Units: meters, seconds, radians.
// Collider dimensions are local half extents, multiplied by authored scale.
struct RigidBox {
    RigidMotion motion = RigidMotion::dynamic_body;
    math::Vec3 half_extents{0.5F, 0.5F, 0.5F};
    math::Vec3 linear_velocity{};
    math::Vec3 angular_velocity{};
    float friction = 0.5F;
    float restitution = 0.0F;
    bool continuous = false;
    bool trigger = false;
};

[[nodiscard]] inline bool valid(const RigidBox& value) noexcept {
    const auto finite_vector = [](const math::Vec3 v) {
        return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
    };
    return (value.motion == RigidMotion::static_body || value.motion == RigidMotion::kinematic_body ||
            value.motion == RigidMotion::dynamic_body) &&
        finite_vector(value.half_extents) && value.half_extents.x > 0 &&
        value.half_extents.y > 0 && value.half_extents.z > 0 &&
        finite_vector(value.linear_velocity) && finite_vector(value.angular_velocity) &&
        std::isfinite(value.friction) && value.friction >= 0 && std::isfinite(value.restitution) &&
        value.restitution >= 0 &&
        (!value.continuous || value.motion == RigidMotion::dynamic_body);
}
} // namespace vice::world
