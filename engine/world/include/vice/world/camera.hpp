#pragma once

#include <cmath>
#include <cstdint>

namespace vice::world {

enum class CameraProjection : std::uint8_t {
    perspective = 1,
};

struct Camera {
    CameraProjection projection = CameraProjection::perspective;
    float vertical_fov_radians = 1.0471975512F;
    float near_plane = 0.05F;
    float far_plane = 1000.0F;
    bool primary = false;

    friend bool operator==(const Camera&, const Camera&) = default;
};

[[nodiscard]] inline bool valid(const Camera& camera) noexcept {
    constexpr float minimum_fov = 0.0174532925F;
    constexpr float maximum_fov = 3.1241393611F;
    return camera.projection == CameraProjection::perspective &&
        std::isfinite(camera.vertical_fov_radians) &&
        std::isfinite(camera.near_plane) &&
        std::isfinite(camera.far_plane) &&
        camera.vertical_fov_radians >= minimum_fov &&
        camera.vertical_fov_radians <= maximum_fov &&
        camera.near_plane > 0.0F &&
        camera.far_plane > camera.near_plane;
}

} // namespace vice::world
