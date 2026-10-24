#include <vice/runtime/ray_ambient_occlusion.hpp>

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

[[nodiscard]] bool close_enough(
    const float left,
    const float right,
    const float epsilon = 1.0e-5F
) {
    return std::abs(left - right) <= epsilon;
}

} // namespace

int main() {
    using namespace vice;
    using namespace vice::runtime;

    constexpr float near_plane = 0.1F;
    constexpr float far_plane = 100.0F;
    CHECK(close_enough(runtime_ray_ambient_occlusion_linear_depth(0.0F, near_plane, far_plane), near_plane));
    CHECK(close_enough(runtime_ray_ambient_occlusion_linear_depth(1.0F, near_plane, far_plane), far_plane, 0.01F));
    CHECK(runtime_ray_ambient_occlusion_linear_depth(0.5F, near_plane, far_plane) > near_plane);
    CHECK(runtime_ray_ambient_occlusion_linear_depth(0.5F, -1.0F, far_plane) == 0.0F);

    CHECK(runtime_ray_ambient_occlusion_preferred_neighbor(
        0.50F, 0.51F, 0.90F, near_plane, far_plane
    ) == RuntimeRayAmbientOcclusionNeighbor::negative);
    CHECK(runtime_ray_ambient_occlusion_preferred_neighbor(
        0.50F, 0.90F, 0.51F, near_plane, far_plane
    ) == RuntimeRayAmbientOcclusionNeighbor::positive);
    CHECK(runtime_ray_ambient_occlusion_preferred_neighbor(
        0.50F, 1.0F, 0.51F, near_plane, far_plane
    ) == RuntimeRayAmbientOcclusionNeighbor::positive);
    CHECK(runtime_ray_ambient_occlusion_preferred_neighbor(
        0.50F, 0.51F, 1.0F, near_plane, far_plane
    ) == RuntimeRayAmbientOcclusionNeighbor::negative);
    CHECK(runtime_ray_ambient_occlusion_preferred_neighbor(
        1.0F, 0.50F, 0.50F, near_plane, far_plane
    ) == RuntimeRayAmbientOcclusionNeighbor::invalid);
    CHECK(runtime_ray_ambient_occlusion_preferred_neighbor(
        0.50F, 1.0F, 1.0F, near_plane, far_plane
    ) == RuntimeRayAmbientOcclusionNeighbor::invalid);

    RuntimeRayAmbientOcclusionCamera camera{
        .position = {10.0F, 20.0F, 30.0F},
        .forward = {0.0F, 0.0F, -1.0F},
        .up = {0.0F, 1.0F, 0.0F},
        .vertical_fov_radians = 1.0F,
        .aspect_ratio = 1.0F,
        .near_plane = near_plane,
        .far_plane = far_plane,
    };

    const auto center = runtime_ray_ambient_occlusion_world_position(
        camera, 1U, 1U, 0U, 0U, 0.0F
    );
    CHECK(center.has_value());
    CHECK(close_enough(center->x, 10.0F));
    CHECK(close_enough(center->y, 20.0F));
    CHECK(close_enough(center->z, 30.0F - near_plane));
    CHECK(!runtime_ray_ambient_occlusion_world_position(
        camera, 1U, 1U, 0U, 0U, 1.0F
    ).has_value());
    CHECK(!runtime_ray_ambient_occlusion_world_position(
        camera, 1U, 1U, 1U, 0U, 0.5F
    ).has_value());

    const math::Vec3 normal{0.0F, 1.0F, 0.0F};
    const auto direction = runtime_ray_ambient_occlusion_hemisphere_direction(
        normal, 123U, 456U, 7U
    );
    CHECK(direction.has_value());
    CHECK(math::dot(*direction, normal) >= -1.0e-5F);
    CHECK(close_enough(math::dot(*direction, *direction), 1.0F, 1.0e-4F));
    const auto repeated = runtime_ray_ambient_occlusion_hemisphere_direction(
        normal, 123U, 456U, 7U
    );
    CHECK(repeated.has_value());
    CHECK(close_enough(direction->x, repeated->x));
    CHECK(close_enough(direction->y, repeated->y));
    CHECK(close_enough(direction->z, repeated->z));
    const auto different = runtime_ray_ambient_occlusion_hemisphere_direction(
        normal, 124U, 456U, 7U
    );
    CHECK(different.has_value());
    CHECK(!close_enough(direction->x, different->x) ||
        !close_enough(direction->y, different->y) ||
        !close_enough(direction->z, different->z));
    CHECK(!runtime_ray_ambient_occlusion_hemisphere_direction(
        math::Vec3{}, 0U, 0U
    ).has_value());

    camera.aspect_ratio = 16.0F / 9.0F;
    camera.jitter_x_ndc = 0.001F;
    camera.jitter_y_ndc = -0.002F;
    const auto constants = make_runtime_ray_ambient_occlusion_constants(
        camera, 1920U, 1080U, 1.75F, 0.003F, 17U
    );
    CHECK(constants.has_value());
    CHECK(close_enough(constants->camera_position_near[0], 10.0F));
    CHECK(close_enough(constants->camera_position_near[3], near_plane));
    CHECK(close_enough(constants->camera_forward_far[2], -1.0F));
    CHECK(close_enough(constants->camera_forward_far[3], far_plane));
    CHECK(close_enough(constants->camera_right_tan_half_fov[0], 1.0F));
    CHECK(close_enough(constants->camera_right_tan_half_fov[3], std::tan(0.5F)));
    CHECK(close_enough(constants->camera_up_aspect[1], 1.0F));
    CHECK(close_enough(constants->camera_up_aspect[3], 16.0F / 9.0F));
    CHECK(close_enough(constants->target_size_jitter[0], 1920.0F));
    CHECK(close_enough(constants->target_size_jitter[1], 1080.0F));
    CHECK(close_enough(constants->target_size_jitter[2], 0.001F));
    CHECK(close_enough(constants->target_size_jitter[3], -0.002F));
    CHECK(close_enough(constants->ao_parameters[0], 1.75F));
    CHECK(close_enough(constants->ao_parameters[1], 0.003F));
    CHECK(close_enough(constants->ao_parameters[2], 17.0F));

    CHECK(!make_runtime_ray_ambient_occlusion_constants(
        camera, 0U, 1080U, 1.0F, 0.002F
    ).has_value());
    CHECK(!make_runtime_ray_ambient_occlusion_constants(
        camera, 1920U, 1080U, 0.0F, 0.002F
    ).has_value());
    CHECK(!make_runtime_ray_ambient_occlusion_constants(
        camera, 1920U, 1080U, 1.0F, 0.0F
    ).has_value());
    CHECK(!make_runtime_ray_ambient_occlusion_constants(
        camera, 1920U, 1080U, 1.0F, 1.0F
    ).has_value());

    return 0;
}

#undef CHECK
