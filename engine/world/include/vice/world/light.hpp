#pragma once

#include <cmath>
#include <cstdint>

namespace vice::world {

enum class LightType : std::uint8_t {
    directional = 1,
    point = 2,
};

struct LightColor {
    float red = 1.0F;
    float green = 1.0F;
    float blue = 1.0F;

    friend bool operator==(const LightColor&, const LightColor&) = default;
};

struct Light {
    LightType type = LightType::directional;
    LightColor color{};
    float intensity = 1.0F;
    float range = 0.0F;
    bool enabled = true;

    friend bool operator==(const Light&, const Light&) = default;
};

[[nodiscard]] inline bool valid(const Light& light) noexcept {
    if (light.type != LightType::directional && light.type != LightType::point) return false;
    if (!std::isfinite(light.color.red) || !std::isfinite(light.color.green) ||
        !std::isfinite(light.color.blue) || light.color.red < 0.0F ||
        light.color.green < 0.0F || light.color.blue < 0.0F ||
        !std::isfinite(light.intensity) || light.intensity < 0.0F ||
        !std::isfinite(light.range) || light.range < 0.0F) {
        return false;
    }
    return light.type != LightType::point || light.range > 0.0F;
}

} // namespace vice::world
