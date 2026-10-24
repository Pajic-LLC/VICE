#pragma once

#include <vice/math/math.hpp>

#include <array>
#include <optional>

namespace vice::runtime {

struct RuntimeReflectionSurfaceGuideSample {
    float encoded_normal_x = 0.5F;
    float encoded_normal_y = 0.5F;
    float roughness = 1.0F;
    float reflectance = 0.0F;

    friend bool operator==(
        const RuntimeReflectionSurfaceGuideSample&,
        const RuntimeReflectionSurfaceGuideSample&
    ) = default;
};

[[nodiscard]] std::optional<std::array<float, 2U>>
runtime_reflection_surface_guide_encode_normal(math::Vec3 normal) noexcept;

[[nodiscard]] std::optional<math::Vec3>
runtime_reflection_surface_guide_decode_normal(float encoded_x, float encoded_y) noexcept;

[[nodiscard]] float runtime_reflection_surface_guide_reflectance_strength(
    math::Vec3 base_color,
    float metallic
) noexcept;

[[nodiscard]] RuntimeReflectionSurfaceGuideSample runtime_reflection_surface_guide_sample(
    math::Vec3 normal,
    float roughness,
    math::Vec3 base_color,
    float metallic
) noexcept;

} // namespace vice::runtime
