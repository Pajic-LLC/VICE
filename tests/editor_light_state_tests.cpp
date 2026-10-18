#include <vice/editor/state.hpp>
#include <vice/world/world.hpp>

#include <cassert>
#include <optional>

int main() {
    using namespace vice;

    world::World world;
    assert(world.register_region(world::RegionDesc{
        .id = world::RegionId{1U},
        .simulation_island = world::SimulationIslandId{1U},
        .bounds = world::RegionBounds{
            .min = {-20.0F, -20.0F, -20.0F},
            .max = {20.0F, 20.0F, 20.0F},
        },
    }));
    const auto id = world::PersistentId::parse("61000000-0000-4000-8000-000000000001");
    assert(id.has_value());
    const auto entity = world.create_entity(*id);
    assert(entity.has_value());
    assert(world.set_spatial_state(*entity, world::SpatialState{
        .global_position = {1.0, 2.0, 3.0},
        .region = world::RegionId{1U},
    }));

    world::Light before_light;
    before_light.type = world::LightType::directional;
    before_light.intensity = 2.0F;
    assert(world.set_light(*entity, before_light));
    const auto before = editor::capture_entity_light(world, *id);
    assert(before.has_value());

    world::Light after_light;
    after_light.type = world::LightType::point;
    after_light.color = {0.5F, 0.6F, 0.7F};
    after_light.intensity = 7.0F;
    after_light.range = 18.0F;
    assert(world.set_light(*entity, after_light));
    const auto after = editor::capture_entity_light(world, *id);
    assert(after.has_value());

    editor::CommandHistory history;
    assert(history.record(editor::CommandHistory::LightEntry{
        .before = *before,
        .after = *after,
        .label = "Edit Light",
    }));
    assert(history.undo(world));
    assert(world.light(*entity) == std::optional{before_light});
    assert(history.redo(world));
    assert(world.light(*entity) == std::optional{after_light});

    const auto hierarchy = editor::build_hierarchy(world);
    assert(hierarchy.size() == 1U);
    assert(hierarchy.front().light == std::optional{after_light});

    const auto inspector = editor::project_inspector(world, *id);
    assert(inspector.has_value());
    assert(inspector->light == std::optional{after_light});
    return 0;
}
