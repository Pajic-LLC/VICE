#include <vice/runtime/scene.hpp>

#include <cassert>
#include <cmath>
#include <span>

namespace {

bool nearly_equal(const float left, const float right, const float tolerance = 0.0001F) {
    return std::abs(left - right) <= tolerance;
}

float luminance(const vice::math::Vec3 value) {
    return value.x * 0.2126F + value.y * 0.7152F + value.z * 0.0722F;
}

bool finite_non_negative(const vice::math::Vec3 value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z) &&
        value.x >= 0.0F && value.y >= 0.0F && value.z >= 0.0F;
}

} // namespace

int main() {
    using namespace vice;

    runtime::RuntimeLight authored_sun;
    authored_sun.light = world::Light{
        .type = world::LightType::directional,
        .color = {1.0F, 0.8F, 0.6F},
        .intensity = 5.0F,
        .range = 0.0F,
        .enabled = true,
    };
    authored_sun.direction = {0.0F, -1.0F, 0.0F};
    const std::array lights{authored_sun};
    const auto frame = runtime::runtime_atmosphere_frame(lights);
    assert(nearly_equal(frame.sun_direction.x, 0.0F));
    assert(nearly_equal(frame.sun_direction.y, 1.0F));
    assert(nearly_equal(frame.sun_direction.z, 0.0F));
    assert(nearly_equal(frame.sun_color.x, 1.0F));
    assert(nearly_equal(frame.sun_color.y, 0.8F));
    assert(nearly_equal(frame.sun_color.z, 0.6F));
    assert(nearly_equal(frame.sun_intensity, 5.0F));

    const auto toward_sun = runtime::evaluate_runtime_sky_radiance(frame.sun_direction, frame);
    const auto away_from_sun = runtime::evaluate_runtime_sky_radiance(
        {0.0F, 0.0F, -1.0F}, frame
    );
    const auto horizon = runtime::evaluate_runtime_sky_radiance(
        math::normalize(math::Vec3{1.0F, 0.02F, 0.0F}), frame
    );
    const auto below_horizon = runtime::evaluate_runtime_sky_radiance(
        math::normalize(math::Vec3{0.0F, -1.0F, 0.0F}), frame
    );
    assert(finite_non_negative(toward_sun));
    assert(finite_non_negative(away_from_sun));
    assert(finite_non_negative(horizon));
    assert(finite_non_negative(below_horizon));
    assert(luminance(toward_sun) > luminance(away_from_sun));
    assert(luminance(horizon) > luminance(below_horizon));

    const auto fallback = runtime::runtime_atmosphere_frame({});
    assert(finite_non_negative(fallback.sun_color));
    assert(fallback.sun_intensity > 0.0F);
    assert(nearly_equal(math::dot(fallback.sun_direction, fallback.sun_direction), 1.0F, 0.001F));

    const float t0 = runtime::runtime_atmosphere_transmittance(0.0F, frame.settings);
    const float t100 = runtime::runtime_atmosphere_transmittance(100.0F, frame.settings);
    const float t1000 = runtime::runtime_atmosphere_transmittance(1000.0F, frame.settings);
    assert(nearly_equal(t0, 1.0F));
    assert(t100 < t0 && t100 > 0.0F);
    assert(t1000 < t100 && t1000 >= 0.0F);

    runtime::RenderMeshInstance far_instance;
    far_instance.spatial.global_position = {
        1'000'000'003.25,
        -2'000'000'004.5,
        3'000'000'005.75,
    };
    const world::GlobalPosition camera_origin{
        1'000'000'000.0,
        -2'000'000'000.0,
        3'000'000'000.0,
    };
    const auto relative = runtime::runtime_model_matrix_relative_to(far_instance, camera_origin);
    assert(nearly_equal(relative(0, 3), 3.25F));
    assert(nearly_equal(relative(1, 3), -4.5F));
    assert(nearly_equal(relative(2, 3), 5.75F));

    return 0;
}
