#pragma once

#include <array>
#include <cmath>
#include <cstddef>

namespace vice::math {

struct Vec3 {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
};

struct Quat {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
    float w = 1.0F;
};

[[nodiscard]] constexpr Vec3 operator-(const Vec3 left, const Vec3 right) noexcept {
    return Vec3{left.x - right.x, left.y - right.y, left.z - right.z};
}

[[nodiscard]] constexpr float dot(const Vec3 left, const Vec3 right) noexcept {
    return left.x * right.x + left.y * right.y + left.z * right.z;
}

[[nodiscard]] constexpr Vec3 cross(const Vec3 left, const Vec3 right) noexcept {
    return Vec3{
        left.y * right.z - left.z * right.y,
        left.z * right.x - left.x * right.z,
        left.x * right.y - left.y * right.x,
    };
}

[[nodiscard]] inline Vec3 normalize(const Vec3 value) noexcept {
    const float length_squared = dot(value, value);
    if (length_squared <= 0.0F) {
        return {};
    }
    const float inverse_length = 1.0F / std::sqrt(length_squared);
    return Vec3{
        value.x * inverse_length,
        value.y * inverse_length,
        value.z * inverse_length,
    };
}

struct Mat4 {
    std::array<float, 16> values{};

    [[nodiscard]] constexpr float& operator()(const std::size_t row, const std::size_t column) noexcept {
        return values[column * 4U + row];
    }

    [[nodiscard]] constexpr const float& operator()(
        const std::size_t row,
        const std::size_t column
    ) const noexcept {
        return values[column * 4U + row];
    }
};

[[nodiscard]] constexpr Mat4 identity() noexcept {
    Mat4 result{};
    result(0, 0) = 1.0F;
    result(1, 1) = 1.0F;
    result(2, 2) = 1.0F;
    result(3, 3) = 1.0F;
    return result;
}

[[nodiscard]] constexpr Mat4 multiply(const Mat4& left, const Mat4& right) noexcept {
    Mat4 result{};
    for (std::size_t column = 0; column < 4U; ++column) {
        for (std::size_t row = 0; row < 4U; ++row) {
            float value = 0.0F;
            for (std::size_t inner = 0; inner < 4U; ++inner) {
                value += left(row, inner) * right(inner, column);
            }
            result(row, column) = value;
        }
    }
    return result;
}

[[nodiscard]] constexpr Mat4 translation(const Vec3 offset) noexcept {
    Mat4 result = identity();
    result(0, 3) = offset.x;
    result(1, 3) = offset.y;
    result(2, 3) = offset.z;
    return result;
}

[[nodiscard]] inline Mat4 rotation(const Quat value) noexcept {
    const float xx = value.x * value.x;
    const float yy = value.y * value.y;
    const float zz = value.z * value.z;
    const float xy = value.x * value.y;
    const float xz = value.x * value.z;
    const float yz = value.y * value.z;
    const float wx = value.w * value.x;
    const float wy = value.w * value.y;
    const float wz = value.w * value.z;

    Mat4 result = identity();
    result(0, 0) = 1.0F - 2.0F * (yy + zz);
    result(0, 1) = 2.0F * (xy - wz);
    result(0, 2) = 2.0F * (xz + wy);
    result(1, 0) = 2.0F * (xy + wz);
    result(1, 1) = 1.0F - 2.0F * (xx + zz);
    result(1, 2) = 2.0F * (yz - wx);
    result(2, 0) = 2.0F * (xz - wy);
    result(2, 1) = 2.0F * (yz + wx);
    result(2, 2) = 1.0F - 2.0F * (xx + yy);
    return result;
}

[[nodiscard]] inline Mat4 pose(const Vec3 position, const Quat orientation) noexcept {
    Mat4 result = rotation(orientation);
    result(0, 3) = position.x;
    result(1, 3) = position.y;
    result(2, 3) = position.z;
    return result;
}

[[nodiscard]] inline Mat4 rotation_y(const float radians) noexcept {
    Mat4 result = identity();
    const float cosine = std::cos(radians);
    const float sine = std::sin(radians);
    result(0, 0) = cosine;
    result(0, 2) = sine;
    result(2, 0) = -sine;
    result(2, 2) = cosine;
    return result;
}

[[nodiscard]] inline Mat4 rotation_x(const float radians) noexcept {
    Mat4 result = identity();
    const float cosine = std::cos(radians);
    const float sine = std::sin(radians);
    result(1, 1) = cosine;
    result(1, 2) = -sine;
    result(2, 1) = sine;
    result(2, 2) = cosine;
    return result;
}

[[nodiscard]] inline Mat4 perspective_rh_zo(
    const float vertical_fov_radians,
    const float aspect_ratio,
    const float near_plane,
    const float far_plane
) noexcept {
    Mat4 result{};
    if (vertical_fov_radians <= 0.0F || aspect_ratio <= 0.0F ||
        near_plane <= 0.0F || far_plane <= near_plane) {
        return result;
    }

    const float focal_length = 1.0F / std::tan(vertical_fov_radians * 0.5F);
    result(0, 0) = focal_length / aspect_ratio;
    result(1, 1) = focal_length;
    result(2, 2) = far_plane / (near_plane - far_plane);
    result(2, 3) = (far_plane * near_plane) / (near_plane - far_plane);
    result(3, 2) = -1.0F;
    return result;
}

[[nodiscard]] inline Mat4 orthographic_rh_zo(
    const float left,
    const float right,
    const float bottom,
    const float top,
    const float near_plane,
    const float far_plane
) noexcept {
    Mat4 result{};
    if (right <= left || top <= bottom || near_plane < 0.0F || far_plane <= near_plane) {
        return result;
    }

    result(0, 0) = 2.0F / (right - left);
    result(1, 1) = 2.0F / (top - bottom);
    result(2, 2) = 1.0F / (near_plane - far_plane);
    result(0, 3) = -(right + left) / (right - left);
    result(1, 3) = -(top + bottom) / (top - bottom);
    result(2, 3) = near_plane / (near_plane - far_plane);
    result(3, 3) = 1.0F;
    return result;
}

[[nodiscard]] inline Mat4 look_at_rh(
    const Vec3 eye,
    const Vec3 target,
    const Vec3 up
) noexcept {
    const Vec3 backward = normalize(eye - target);
    const Vec3 right = normalize(cross(up, backward));
    const Vec3 camera_up = cross(backward, right);

    Mat4 result = identity();
    result(0, 0) = right.x;
    result(0, 1) = right.y;
    result(0, 2) = right.z;
    result(0, 3) = -dot(right, eye);

    result(1, 0) = camera_up.x;
    result(1, 1) = camera_up.y;
    result(1, 2) = camera_up.z;
    result(1, 3) = -dot(camera_up, eye);

    result(2, 0) = backward.x;
    result(2, 1) = backward.y;
    result(2, 2) = backward.z;
    result(2, 3) = -dot(backward, eye);
    return result;
}

} // namespace vice::math
