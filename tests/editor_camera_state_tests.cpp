#include <vice/editor/state.hpp>

#include <cstdlib>
#include <iostream>
#include <string_view>

namespace {

void require(const bool condition, const std::string_view message) {
    if (condition) return;
    std::cerr << "editor camera state test failed: " << message << '\n';
    std::exit(1);
}

[[nodiscard]] vice::world::PersistentId fixed_id(const std::uint8_t tail) {
    vice::world::PersistentId::Bytes bytes{};
    bytes[15] = tail;
    return vice::world::PersistentId{bytes};
}

} // namespace

int main() {
    using namespace vice;

    world::World world;
    const world::RegionDesc region{
        .id = world::RegionId{1},
        .simulation_island = world::SimulationIslandId{1},
        .origin = {},
        .streaming_cell = {},
        .fidelity = world::RegionFidelity::full,
        .bounds = world::RegionBounds{
            .min = {-100.0F, -100.0F, -100.0F},
            .max = {100.0F, 100.0F, 100.0F},
        },
    };
    require(world.register_region(region), "camera test region registers");

    const auto first_id = fixed_id(41);
    const auto second_id = fixed_id(42);
    const auto first = world.create_entity(first_id);
    const auto second = world.create_entity(second_id);
    require(first.has_value() && second.has_value(), "camera entities create");
    require(
        world.set_spatial_state(
            *first,
            world::SpatialState{.global_position = {0.0, 2.0, 8.0}, .region = region.id}
        ),
        "first camera spatial state"
    );
    require(
        world.set_spatial_state(
            *second,
            world::SpatialState{.global_position = {4.0, 3.0, 6.0}, .region = region.id}
        ),
        "second camera spatial state"
    );

    const world::Camera primary{
        .projection = world::CameraProjection::perspective,
        .vertical_fov_radians = 1.0471975512F,
        .near_plane = 0.05F,
        .far_plane = 500.0F,
        .primary = true,
    };
    auto secondary = primary;
    secondary.primary = false;
    secondary.vertical_fov_radians = 0.9F;

    require(world.set_camera(*first, primary), "first camera becomes primary");
    require(world.set_camera(*second, secondary), "second camera authored secondary");
    require(world.primary_camera() == first, "first camera initially primary");

    auto hierarchy = editor::build_hierarchy(world);
    require(hierarchy.size() == 2U, "hierarchy contains camera entities");
    require(hierarchy[0].camera.has_value(), "hierarchy projects first camera");
    require(hierarchy[1].camera.has_value(), "hierarchy projects second camera");

    const auto first_inspector = editor::project_inspector(world, first_id);
    require(first_inspector.has_value(), "first camera inspector projects");
    require(first_inspector->camera == std::optional{primary}, "inspector camera data");

    editor::CommandHistory history;
    history.mark_saved();

    const auto first_before = editor::capture_entity_camera(world, first_id);
    const auto second_before = editor::capture_entity_camera(world, second_id);
    require(first_before.has_value() && second_before.has_value(), "camera state captures");

    auto first_after = *first_before;
    first_after.camera->primary = false;
    auto second_after = *second_before;
    second_after.camera->primary = true;
    second_after.camera->vertical_fov_radians = 1.2F;

    const std::vector<editor::EntityCameraState> before{*first_before, *second_before};
    const std::vector<editor::EntityCameraState> after{first_after, second_after};
    require(editor::apply_camera_states(world, after), "primary camera handoff applies atomically");
    require(world.primary_camera() == second, "second camera becomes primary");
    require(world.camera(*first)->primary == false, "first camera demoted");
    require(world.camera(*second)->vertical_fov_radians == 1.2F, "camera field edit applies");

    require(
        history.record(editor::CommandHistory::CameraEntry{
            .before = before,
            .after = after,
            .label = "Make Primary Camera",
        }),
        "camera handoff records"
    );
    require(history.dirty(), "camera edit marks history dirty");
    require(history.undo_label() == "Make Primary Camera", "camera undo label");

    require(history.undo(world), "camera handoff undo");
    require(world.primary_camera() == first, "undo restores original primary camera");
    require(world.camera(*first) == first_before->camera, "undo restores first camera data");
    require(world.camera(*second) == second_before->camera, "undo restores second camera data");
    require(!history.dirty(), "undo returns to saved camera state");

    require(history.redo(world), "camera handoff redo");
    require(world.primary_camera() == second, "redo restores second primary camera");
    require(world.camera(*first) == first_after.camera, "redo restores first camera data");
    require(world.camera(*second) == second_after.camera, "redo restores second camera data");

    auto illegal_primary = primary;
    require(!world.set_camera(*first, illegal_primary), "World rejects a second primary camera");

    require(world.clear_spatial_state(*second), "clearing primary camera spatial state");
    require(!world.camera(*second).has_value(), "camera clears with spatial state");
    require(!world.primary_camera().has_value(), "no primary remains after camera spatial clear");

    return 0;
}
