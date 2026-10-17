#include <vice/editor/ui.hpp>

#include <cassert>

namespace {

vice::world::PersistentId id(const char* text) {
    const auto parsed = vice::world::PersistentId::parse(text);
    assert(parsed.has_value());
    return *parsed;
}

void test_workspace_frame() {
    using namespace vice;

    world::World world;
    const world::RegionDesc region{
        .id = world::RegionId{1},
        .simulation_island = world::SimulationIslandId{10},
        .origin = {0.0, 0.0, 0.0},
        .streaming_cell = {0, 0, 0},
        .fidelity = world::RegionFidelity::full,
    };
    assert(world.register_region(region));

    const auto persistent = id("11111111-1111-4111-8111-111111111111");
    const auto entity = world.create_entity(persistent);
    assert(entity.has_value());
    assert(world.set_spatial_state(
        *entity,
        world::SpatialState{
            .global_position = {1.0, 2.0, 3.0},
            .region = region.id,
        }
    ));

    editor::SelectionModel selection;
    selection.select(persistent);
    editor::ConsoleHistory console{16};
    console.push(core::LogRecord{core::LogLevel::info, "Editor UI test", {}});

    editor::EditorUi ui;
    assert(ui.valid());
    assert(ui.begin_frame(
        platform::WindowMetrics{
            .width = 1280,
            .height = 800,
            .drawable_width = 2560,
            .drawable_height = 1600,
            .scale_factor = 2.0F,
        },
        1.0F / 60.0F
    ));

    ui.draw(
        world,
        selection,
        console,
        editor::UiStatus{
            .world_name = "Editor Test World",
            .render_backend = "Test Renderer",
            .world_loaded = true,
        }
    );
    ui.end_frame();

    const auto scene = ui.scene_viewport();
    assert(scene.drawable());
    assert(scene.framebuffer_scale == 2.0F);
    assert(selection.selected() == persistent);
    assert(!ui.consume_quit_request());
}

} // namespace

int main() {
    test_workspace_frame();
    return 0;
}
