#include "hiz_history.hpp"

#include <cassert>
#include <vector>

namespace {

[[nodiscard]] vice::world::PersistentId required_id(const char* const text) {
    const auto parsed = vice::world::PersistentId::parse(text);
    assert(parsed.has_value());
    return *parsed;
}

[[nodiscard]] vice::world::ResourceId resource_id(const char* const text) {
    return vice::world::ResourceId{required_id(text)};
}

} // namespace

int main() {
    using namespace vice;
    using namespace vice::runtime;
    using namespace vice::runtime::detail;

    const auto camera_id = required_id("91000000-0000-4000-8000-000000000001");
    const auto entity_a = required_id("92000000-0000-4000-8000-000000000001");
    const auto entity_b = required_id("92000000-0000-4000-8000-000000000002");
    const auto mesh_a = resource_id("93000000-0000-4000-8000-000000000001");
    const auto mesh_b = resource_id("93000000-0000-4000-8000-000000000002");

    world::Camera camera{
        .projection = world::CameraProjection::perspective,
        .vertical_fov_radians = 1.0F,
        .near_plane = 0.1F,
        .far_plane = 500.0F,
        .primary = true,
    };
    const world::GlobalPosition camera_position{1000000.0, -250000.0, 3000000.0};
    const math::Vec3 forward{0.0F, 0.0F, -1.0F};
    const math::Vec3 up{0.0F, 1.0F, 0.0F};

    std::vector<RenderMeshInstance> instances{
        RenderMeshInstance{
            .entity = entity_a,
            .mesh = mesh_a,
            .spatial = world::SpatialState{
                .global_position = {1000005.0, -250000.0, 2999980.0},
            },
        },
        RenderMeshInstance{
            .entity = entity_b,
            .mesh = mesh_b,
            .spatial = world::SpatialState{
                .global_position = {999995.0, -250002.0, 2999960.0},
            },
        },
    };

    RuntimeHiZHistory history;
    assert(!history.usable(camera_id, camera_position, forward, up, camera, 16.0F / 9.0F, instances));

    const auto relative_view_projection = math::multiply(
        math::perspective_rh_zo(
            camera.vertical_fov_radians,
            16.0F / 9.0F,
            camera.near_plane,
            camera.far_plane
        ),
        math::look_at_rh({}, forward, up)
    );
    history.commit(
        camera_id,
        camera_position,
        forward,
        up,
        camera,
        16.0F / 9.0F,
        relative_view_projection,
        instances
    );
    assert(history.usable(camera_id, camera_position, forward, up, camera, 16.0F / 9.0F, instances));
    assert(history.camera().valid);
    assert(history.camera().global_position == camera_position);

    const world::GlobalPosition small_move{
        camera_position.x + 1.0,
        camera_position.y,
        camera_position.z,
    };
    assert(history.usable(camera_id, small_move, forward, up, camera, 16.0F / 9.0F, instances));

    const world::GlobalPosition camera_cut{
        camera_position.x + static_cast<double>(runtime_taa_camera_cut_distance) + 1.0,
        camera_position.y,
        camera_position.z,
    };
    assert(!history.usable(camera_id, camera_cut, forward, up, camera, 16.0F / 9.0F, instances));
    assert(!history.usable(camera_id, camera_position, {0.0F, 0.0F, 1.0F}, up,
        camera, 16.0F / 9.0F, instances));
    assert(!history.usable(camera_id, camera_position, forward, {0.0F, -1.0F, 0.0F},
        camera, 16.0F / 9.0F, instances));

    auto changed_camera = camera;
    changed_camera.vertical_fov_radians += 0.1F;
    assert(!history.usable(camera_id, camera_position, forward, up,
        changed_camera, 16.0F / 9.0F, instances));

    auto moved = instances;
    moved[0].spatial.global_position.x += 0.01;
    assert(!history.usable(camera_id, camera_position, forward, up,
        camera, 16.0F / 9.0F, moved));

    auto changed_mesh = instances;
    changed_mesh[1].mesh = mesh_a;
    assert(!history.usable(camera_id, camera_position, forward, up,
        camera, 16.0F / 9.0F, changed_mesh));

    auto removed = instances;
    removed.pop_back();
    assert(!history.usable(camera_id, camera_position, forward, up,
        camera, 16.0F / 9.0F, removed));

    auto added = instances;
    added.push_back(RenderMeshInstance{
        .entity = required_id("92000000-0000-4000-8000-000000000003"),
        .mesh = mesh_a,
    });
    assert(!history.usable(camera_id, camera_position, forward, up,
        camera, 16.0F / 9.0F, added));

    history.reset();
    assert(!history.usable(camera_id, camera_position, forward, up,
        camera, 16.0F / 9.0F, instances));
    return 0;
}
