#include <vice/runtime/directional_shadow.hpp>
#include <vice/runtime/scene.hpp>
#include <vice/world/world.hpp>

#include <array>
#include <cassert>
#include <cmath>
#include <optional>

namespace {

vice::world::PersistentId id(const std::size_t index) {
    static constexpr std::array<const char*, 10> ids{
        "61000000-0000-4000-8000-000000000001",
        "61000000-0000-4000-8000-000000000002",
        "61000000-0000-4000-8000-000000000003",
        "61000000-0000-4000-8000-000000000004",
        "61000000-0000-4000-8000-000000000005",
        "61000000-0000-4000-8000-000000000006",
        "61000000-0000-4000-8000-000000000007",
        "61000000-0000-4000-8000-000000000008",
        "61000000-0000-4000-8000-000000000009",
        "61000000-0000-4000-8000-000000000010",
    };
    const auto parsed = vice::world::PersistentId::parse(ids.at(index));
    assert(parsed.has_value());
    return *parsed;
}

static_assert(
    vice::runtime::resolve_runtime_directional_shadow_path(
        vice::runtime::RuntimeDirectionalShadowMode::automatic,
        false
    ) == vice::runtime::RuntimeDirectionalShadowPath::cascaded
);
static_assert(
    vice::runtime::resolve_runtime_directional_shadow_path(
        vice::runtime::RuntimeDirectionalShadowMode::automatic,
        true
    ) == vice::runtime::RuntimeDirectionalShadowPath::ray_traced
);
static_assert(
    vice::runtime::resolve_runtime_directional_shadow_path(
        vice::runtime::RuntimeDirectionalShadowMode::cascaded,
        true
    ) == vice::runtime::RuntimeDirectionalShadowPath::cascaded
);
static_assert(
    vice::runtime::resolve_runtime_directional_shadow_path(
        vice::runtime::RuntimeDirectionalShadowMode::ray_traced,
        false
    ) == vice::runtime::RuntimeDirectionalShadowPath::cascaded
);
static_assert(
    vice::runtime::resolve_runtime_directional_shadow_path(
        vice::runtime::RuntimeDirectionalShadowMode::ray_traced,
        true
    ) == vice::runtime::RuntimeDirectionalShadowPath::ray_traced
);

} // namespace

int main() {
    using namespace vice;

    world::World scene;
    assert(scene.register_region(world::RegionDesc{
        .id = world::RegionId{1U},
        .simulation_island = world::SimulationIslandId{1U},
        .bounds = world::RegionBounds{
            .min = {-50.0F, -50.0F, -50.0F},
            .max = {50.0F, 50.0F, 50.0F},
        },
    }));

    for (std::size_t index = 0; index < 10U; ++index) {
        const auto entity = scene.create_entity(id(index));
        assert(entity.has_value());
        assert(scene.set_spatial_state(*entity, world::SpatialState{
            .global_position = {static_cast<double>(index + 1U), 2.0, 3.0},
            .region = world::RegionId{1U},
        }));
        world::Light light;
        light.type = index == 0U ? world::LightType::directional : world::LightType::point;
        light.range = light.type == world::LightType::point ? 20.0F : 0.0F;
        light.intensity = static_cast<float>(index + 1U);
        assert(scene.set_light(*entity, light));
    }

    auto lights = runtime::collect_runtime_lights(scene);
    assert(lights.size() == runtime::max_runtime_lights);
    assert(lights.front().entity == id(0));
    assert(lights.back().entity == id(7));
    assert(lights.front().light.type == world::LightType::directional);
    assert(runtime::select_runtime_shadow_light(lights) == std::optional<std::size_t>{0U});
    assert(std::abs(lights.front().direction.x) < 0.0001F);
    assert(std::abs(lights.front().direction.y) < 0.0001F);
    assert(std::abs(lights.front().direction.z + 1.0F) < 0.0001F);

    const auto first = scene.find(id(0));
    assert(first.has_value());
    assert(scene.set_authored_transform(*first, world::AuthoredTransform{
        .rotation = {0.0F, 1.0F, 0.0F, 0.0F},
    }));
    lights = runtime::collect_runtime_lights(scene);
    assert(std::abs(lights.front().direction.z - 1.0F) < 0.0001F);

    const auto second = scene.find(id(1));
    assert(second.has_value());
    assert(scene.set_spatial_state(*second, world::SpatialState{
        .global_position = {17.0, 4.0, -2.0},
        .region = world::RegionId{1U},
    }));
    lights = runtime::collect_runtime_lights(scene);
    assert(lights[1].position.x == 17.0F);
    assert(lights[1].position.y == 4.0F);
    assert(lights[1].position.z == -2.0F);

    auto disabled = scene.light(*first);
    assert(disabled.has_value());
    disabled->enabled = false;
    assert(scene.set_light(*first, *disabled));
    lights = runtime::collect_runtime_lights(scene);
    assert(lights.front().entity == id(1));
    assert(lights.back().entity == id(8));
    assert(!runtime::select_runtime_shadow_light(lights).has_value());
    return 0;
}
