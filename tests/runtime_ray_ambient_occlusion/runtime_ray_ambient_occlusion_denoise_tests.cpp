#include <vice/runtime/ray_ambient_occlusion_denoise.hpp>

#include <cmath>
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
    using namespace vice::runtime;

    constexpr float near_plane = 0.1F;
    constexpr float far_plane = 100.0F;

    const float center_weight = runtime_ray_ambient_occlusion_bilateral_weight(
        0.5F, 0.5F, 0.0F, near_plane, far_plane
    );
    CHECK(close_enough(center_weight, 1.0F));

    const float adjacent_same_surface = runtime_ray_ambient_occlusion_bilateral_weight(
        0.5F, 0.5F, 1.0F, near_plane, far_plane
    );
    CHECK(adjacent_same_surface > 0.0F);
    CHECK(adjacent_same_surface < center_weight);

    const float mild_depth_change = runtime_ray_ambient_occlusion_bilateral_weight(
        0.5F, 0.501F, 1.0F, near_plane, far_plane
    );
    const float silhouette_depth_change = runtime_ray_ambient_occlusion_bilateral_weight(
        0.5F, 0.8F, 1.0F, near_plane, far_plane
    );
    CHECK(mild_depth_change > silhouette_depth_change);
    CHECK(silhouette_depth_change < 1.0e-4F);

    CHECK(runtime_ray_ambient_occlusion_bilateral_weight(
        1.0F, 0.5F, 0.0F, near_plane, far_plane
    ) == 0.0F);
    CHECK(runtime_ray_ambient_occlusion_bilateral_weight(
        0.5F, 1.0F, 0.0F, near_plane, far_plane
    ) == 0.0F);
    CHECK(runtime_ray_ambient_occlusion_bilateral_weight(
        0.5F, 0.5F, -1.0F, near_plane, far_plane
    ) == 0.0F);

    const auto constants = make_runtime_ray_ambient_occlusion_denoise_constants(
        1920U, 1080U, near_plane, far_plane, 0.02F, 1.25F, 2U
    );
    CHECK(constants.has_value());
    CHECK(close_enough(constants->target_size_near_far[0], 1920.0F));
    CHECK(close_enough(constants->target_size_near_far[1], 1080.0F));
    CHECK(close_enough(constants->target_size_near_far[2], near_plane));
    CHECK(close_enough(constants->target_size_near_far[3], far_plane));
    CHECK(close_enough(constants->filter_parameters[0], 0.02F));
    CHECK(close_enough(constants->filter_parameters[1], 1.25F));
    CHECK(close_enough(constants->filter_parameters[2], 2.0F));

    CHECK(!make_runtime_ray_ambient_occlusion_denoise_constants(
        0U, 1080U, near_plane, far_plane
    ).has_value());
    CHECK(!make_runtime_ray_ambient_occlusion_denoise_constants(
        1920U, 1080U, -1.0F, far_plane
    ).has_value());
    CHECK(!make_runtime_ray_ambient_occlusion_denoise_constants(
        1920U, 1080U, near_plane, far_plane, 0.0F, 1.25F, 2U
    ).has_value());
    CHECK(!make_runtime_ray_ambient_occlusion_denoise_constants(
        1920U, 1080U, near_plane, far_plane, 0.02F, 1.25F, 5U
    ).has_value());

    return 0;
}

#undef CHECK
