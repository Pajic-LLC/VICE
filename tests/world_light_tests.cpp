#include <vice/world/world.hpp>

#include <cassert>
#include <limits>
#include <optional>

int main() {
    using namespace vice::world;

    World world;
    assert(world.register_region(RegionDesc{
        .id = RegionId{1U},
        .simulation_island = SimulationIslandId{1U},
        .bounds = RegionBounds{
            .min = {-20.0F, -20.0F, -20.0F},
            .max = {20.0F, 20.0F, 20.0F},
        },
    }));

    const auto entity = world.create_entity();
    assert(!world.set_light(entity, Light{}));
    assert(world.set_spatial_state(entity, SpatialState{.global_position = {}, .region = RegionId{1U}}));

    Light directional;
    directional.type = LightType::directional;
    directional.color = {1.0F, 0.5F, 0.25F};
    directional.intensity = 3.0F;
    assert(world.set_light(entity, directional));
    assert(world.light(entity) == std::optional{directional});

    Light invalid = directional;
    invalid.intensity = std::numeric_limits<float>::quiet_NaN();
    assert(!world.set_light(entity, invalid));
    invalid = directional;
    invalid.type = LightType::point;
    invalid.range = 0.0F;
    assert(!world.set_light(entity, invalid));

    Light point = directional;
    point.type = LightType::point;
    point.range = 12.0F;
    assert(world.set_light(entity, point));
    assert(world.light(entity) == std::optional{point});

    const auto snapshot = world.snapshot();
    assert(snapshot.entities.size() == 1U);
    assert(snapshot.entities[0].light == std::optional{point});
    World restored;
    assert(restored.restore(snapshot));
    const auto restored_entity = restored.find(snapshot.entities[0].persistent_id);
    assert(restored_entity.has_value());
    assert(restored.light(*restored_entity) == std::optional{point});

    assert(world.clear_spatial_state(entity));
    assert(!world.light(entity).has_value());
    return 0;
}
