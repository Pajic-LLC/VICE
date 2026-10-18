#include <vice/math/math.hpp>

#include <cassert>
#include <cmath>

namespace {

[[nodiscard]] bool near(const float left, const float right, const float epsilon = 0.0001F) {
    return std::fabs(left - right) <= epsilon;
}

} // namespace

int main() {
    using namespace vice::math;

    constexpr Mat4 identity_matrix = identity();
    static_assert(identity_matrix(0, 0) == 1.0F);
    static_assert(identity_matrix(1, 1) == 1.0F);
    static_assert(identity_matrix(2, 2) == 1.0F);
    static_assert(identity_matrix(3, 3) == 1.0F);

    constexpr Mat4 moved = translation(Vec3{1.0F, 2.0F, 3.0F});
    static_assert(moved(0, 3) == 1.0F);
    static_assert(moved(1, 3) == 2.0F);
    static_assert(moved(2, 3) == 3.0F);

    constexpr Mat4 composed = multiply(identity_matrix, moved);
    static_assert(composed(0, 3) == 1.0F);
    static_assert(composed(1, 3) == 2.0F);
    static_assert(composed(2, 3) == 3.0F);

    const Mat4 quaternion_identity = rotation(Quat{});
    assert(near(quaternion_identity(0, 0), 1.0F));
    assert(near(quaternion_identity(1, 1), 1.0F));
    assert(near(quaternion_identity(2, 2), 1.0F));

    const float half_angle = 0.7853981634F;
    const Mat4 quaternion_turn = rotation(Quat{
        0.0F,
        std::sin(half_angle),
        0.0F,
        std::cos(half_angle),
    });
    assert(near(quaternion_turn(0, 0), 0.0F));
    assert(near(quaternion_turn(0, 2), 1.0F));
    assert(near(quaternion_turn(2, 0), -1.0F));

    const Mat4 body_pose = pose(Vec3{2.0F, 3.0F, -4.0F}, Quat{});
    assert(near(body_pose(0, 3), 2.0F));
    assert(near(body_pose(1, 3), 3.0F));
    assert(near(body_pose(2, 3), -4.0F));

    const Mat4 view = look_at_rh(
        Vec3{0.0F, 0.0F, 3.0F},
        Vec3{0.0F, 0.0F, 0.0F},
        Vec3{0.0F, 1.0F, 0.0F}
    );
    assert(near(view(0, 0), 1.0F));
    assert(near(view(1, 1), 1.0F));
    assert(near(view(2, 2), 1.0F));
    assert(near(view(2, 3), -3.0F));

    const Mat4 orthographic = orthographic_rh_zo(-2.0F, 2.0F, -1.0F, 3.0F, 0.5F, 10.5F);
    assert(near(orthographic(0, 0), 0.5F));
    assert(near(orthographic(1, 1), 0.5F));
    assert(near(orthographic(2, 2), -0.1F));
    assert(near(orthographic(1, 3), -0.5F));
    assert(near(orthographic(2, 3), -0.05F));
    assert(near(orthographic(3, 3), 1.0F));

    const Mat4 projection = perspective_rh_zo(
        1.0471975512F,
        16.0F / 9.0F,
        0.1F,
        100.0F
    );
    assert(projection(0, 0) > 0.0F);
    assert(projection(1, 1) > 0.0F);
    assert(projection(2, 2) < 0.0F);
    assert(projection(3, 2) == -1.0F);

    const Mat4 rotation_matrix = multiply(rotation_y(0.4F), rotation_x(-0.25F));
    const Mat4 mvp = multiply(projection, multiply(view, rotation_matrix));
    assert(std::isfinite(mvp(0, 0)));
    assert(std::isfinite(mvp(3, 2)));

    const Vec3 normalized = normalize(Vec3{3.0F, 0.0F, 4.0F});
    assert(near(normalized.x, 0.6F));
    assert(near(normalized.z, 0.8F));

    return 0;
}
