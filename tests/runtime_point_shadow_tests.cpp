#include <vice/runtime/scene.hpp>

#include <array>
#include <cassert>
#include <cmath>

namespace {

bool finite_matrix(const vice::math::Mat4& matrix) {
    for (std::size_t row = 0U; row < 4U; ++row) {
        for (std::size_t column = 0U; column < 4U; ++column) {
            if (!std::isfinite(matrix(row, column))) return false;
        }
    }
    return true;
}

vice::runtime::RuntimeLight point_light(
    const vice::world::GlobalPosition position,
    const float range,
    const float intensity = 1.0F
) {
    vice::runtime::RuntimeLight light;
    light.light = vice::world::Light{
        .type = vice::world::LightType::point,
        .color = {1.0F, 1.0F, 1.0F},
        .intensity = intensity,
        .range = range,
        .enabled = true,
    };
    light.global_position = position;
    light.position = {
        static_cast<float>(position.x),
        static_cast<float>(position.y),
        static_cast<float>(position.z),
    };
    return light;
}

} // namespace

int main() {
    using namespace vice;

    assert(runtime::runtime_point_shadow_face({1.0F, 0.1F, 0.1F}) ==
        runtime::RuntimePointShadowFace::positive_x);
    assert(runtime::runtime_point_shadow_face({-2.0F, 0.1F, 0.2F}) ==
        runtime::RuntimePointShadowFace::negative_x);
    assert(runtime::runtime_point_shadow_face({0.1F, 4.0F, 0.2F}) ==
        runtime::RuntimePointShadowFace::positive_y);
    assert(runtime::runtime_point_shadow_face({0.1F, -4.0F, 0.2F}) ==
        runtime::RuntimePointShadowFace::negative_y);
    assert(runtime::runtime_point_shadow_face({0.1F, 0.2F, 5.0F}) ==
        runtime::RuntimePointShadowFace::positive_z);
    assert(runtime::runtime_point_shadow_face({0.1F, 0.2F, -5.0F}) ==
        runtime::RuntimePointShadowFace::negative_z);

    for (std::size_t face = 0U; face < runtime::runtime_point_shadow_face_count; ++face) {
        const auto matrix = runtime::runtime_point_shadow_face_view_projection(
            static_cast<runtime::RuntimePointShadowFace>(face), 25.0F
        );
        assert(finite_matrix(matrix));
    }

    const world::GlobalPosition camera{
        1'000'000'000.0,
        -2'000'000'000.0,
        3'000'000'000.0,
    };
    std::array lights{
        point_light({camera.x + 5.0, camera.y, camera.z}, 20.0F, 2.0F),
        point_light({camera.x + 50.0, camera.y, camera.z}, 100.0F, 5.0F),
        point_light({camera.x + 3.0, camera.y, camera.z}, 5.0F, 10.0F),
    };
    const auto views = runtime::runtime_point_shadow_views(camera, lights);
    assert(views[0].has_value());
    assert(views[1].has_value());
    assert(views[0]->light_index == 0U);
    assert(views[1]->light_index == 1U);
    assert(views[0]->global_position.x == camera.x + 5.0);
    assert(views[0]->range == 20.0F);

    lights[0].light.enabled = false;
    const auto without_first = runtime::runtime_point_shadow_views(camera, lights);
    assert(without_first[0].has_value());
    assert(without_first[0]->light_index == 1U);

    runtime::RuntimeLight directional;
    directional.light = world::Light{
        .type = world::LightType::directional,
        .color = {1.0F, 1.0F, 1.0F},
        .intensity = 10.0F,
        .range = 0.0F,
        .enabled = true,
    };
    const std::array mixed{directional, point_light({camera.x + 2.0, camera.y, camera.z}, 12.0F)};
    const auto mixed_views = runtime::runtime_point_shadow_views(camera, mixed);
    assert(mixed_views[0].has_value());
    assert(mixed_views[0]->light_index == 1U);

    runtime::RenderMeshInstance far_instance;
    far_instance.spatial.global_position = {
        camera.x + 7.25,
        camera.y - 3.5,
        camera.z + 1.75,
    };
    const auto light_relative = runtime::runtime_model_matrix_relative_to(
        far_instance,
        {camera.x + 5.0, camera.y, camera.z}
    );
    assert(std::abs(light_relative(0, 3) - 2.25F) < 0.0001F);
    assert(std::abs(light_relative(1, 3) + 3.5F) < 0.0001F);
    assert(std::abs(light_relative(2, 3) - 1.75F) < 0.0001F);

    return 0;
}
