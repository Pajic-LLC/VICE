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

    const Mat4 view = look_at_rh(
        Vec3{0.0F, 0.0F, 3.0F},
        Vec3{0.0F, 0.0F, 0.0F},
        Vec3{0.0F, 1.0F, 0.0F}
    );
    assert(near(view(0, 0), 1.0F));
    assert(near(view(1, 1), 1.0F));
    assert(near(view(2, 2), 1.0F));
    assert(near(view(2, 3), -3.0F));

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

    const Mat4 rotation = multiply(rotation_y(0.4F), rotation_x(-0.25F));
    const Mat4 mvp = multiply(projection, multiply(view, rotation));
    assert(std::isfinite(mvp(0, 0)));
    assert(std::isfinite(mvp(3, 2)));

    const Vec3 normalized = normalize(Vec3{3.0F, 0.0F, 4.0F});
    assert(near(normalized.x, 0.6F));
    assert(near(normalized.z, 0.8F));

    return 0;
}
