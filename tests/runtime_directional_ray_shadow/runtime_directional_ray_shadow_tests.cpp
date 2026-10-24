#include <vice/runtime/directional_ray_shadow.hpp>

#include <cmath>
#include <cstdint>
#include <iostream>

#define CHECK(condition) do { \
    if (!(condition)) { \
        std::cerr << "CHECK failed at line " << __LINE__ << ": " #condition << '\n'; \
        return __LINE__; \
    } \
} while (false)

namespace {

[[nodiscard]] bool close_enough(const float left, const float right, const float epsilon = 1.0e-5F) {
    return std::abs(left - right) <= epsilon;
}

} // namespace

int main() {
    using namespace vice;
    using namespace vice::runtime;

    constexpr float near_plane = 0.1F;
    constexpr float far_plane = 100.0F;
    CHECK(close_enough(runtime_directional_ray_shadow_linear_depth(0.0F, near_plane, far_plane), near_plane));
    CHECK(close_enough(runtime_directional_ray_shadow_linear_depth(1.0F, near_plane, far_plane), far_plane, 0.01F));
    CHECK(runtime_directional_ray_shadow_linear_depth(0.5F, near_plane, far_plane) > near_plane);
    CHECK(runtime_directional_ray_shadow_linear_depth(0.5F, near_plane, far_plane) < far_plane);
    CHECK(runtime_directional_ray_shadow_linear_depth(0.5F, -1.0F, far_plane) == 0.0F);

    RuntimeDirectionalRayShadowCamera camera{
        .position = {10.0F, 20.0F, 30.0F},
        .forward = {0.0F, 0.0F, -1.0F},
        .up = {0.0F, 1.0F, 0.0F},
        .vertical_fov_radians = 1.0F,
        .aspect_ratio = 16.0F / 9.0F,
        .near_plane = near_plane,
        .far_plane = far_plane,
        .jitter_x_ndc = 0.001F,
        .jitter_y_ndc = -0.002F,
    };

    const auto constants = make_runtime_directional_ray_shadow_constants(
        camera,
        math::Vec3{0.0F, 1.0F, 0.0F},
        1920U,
        1080U,
        0.003F
    );
    CHECK(constants.has_value());
    CHECK(close_enough(constants->camera_position_near[0], 10.0F));
    CHECK(close_enough(constants->camera_position_near[1], 20.0F));
    CHECK(close_enough(constants->camera_position_near[2], 30.0F));
    CHECK(close_enough(constants->camera_position_near[3], near_plane));
    CHECK(close_enough(constants->camera_forward_far[2], -1.0F));
    CHECK(close_enough(constants->camera_forward_far[3], far_plane));
    CHECK(close_enough(constants->camera_right_tan_half_fov[0], 1.0F));
    CHECK(close_enough(constants->camera_right_tan_half_fov[1], 0.0F));
    CHECK(close_enough(constants->camera_right_tan_half_fov[2], 0.0F));
    CHECK(close_enough(constants->camera_right_tan_half_fov[3], std::tan(0.5F)));
    CHECK(close_enough(constants->camera_up_aspect[0], 0.0F));
    CHECK(close_enough(constants->camera_up_aspect[1], 1.0F));
    CHECK(close_enough(constants->camera_up_aspect[2], 0.0F));
    CHECK(close_enough(constants->camera_up_aspect[3], 16.0F / 9.0F));
    CHECK(close_enough(constants->light_direction_bias[1], 1.0F));
    CHECK(close_enough(constants->light_direction_bias[3], 0.003F));
    CHECK(close_enough(constants->target_size_jitter[0], 1920.0F));
    CHECK(close_enough(constants->target_size_jitter[1], 1080.0F));
    CHECK(close_enough(constants->target_size_jitter[2], 0.001F));
    CHECK(close_enough(constants->target_size_jitter[3], -0.002F));

    auto invalid_camera = camera;
    invalid_camera.up = {0.0F, 0.0F, -1.0F};
    CHECK(!make_runtime_directional_ray_shadow_constants(
        invalid_camera,
        math::Vec3{0.0F, 1.0F, 0.0F},
        1920U,
        1080U
    ).has_value());
    CHECK(!make_runtime_directional_ray_shadow_constants(
        camera,
        math::Vec3{},
        1920U,
        1080U
    ).has_value());
    CHECK(!make_runtime_directional_ray_shadow_constants(
        camera,
        math::Vec3{0.0F, 1.0F, 0.0F},
        0U,
        1080U
    ).has_value());

    return 0;
}

#undef CHECK