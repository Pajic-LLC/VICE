#include <vice/rigid_body/world_bridge.hpp>

#include <cassert>
#include <cmath>

namespace {

bool near(const double left, const double right, const double tolerance = 0.001) {
    return std::abs(left - right) <= tolerance;
}

bool near(const float left, const float right, const float tolerance = 0.001F) {
    return std::abs(left - right) <= tolerance;
}

} // namespace

int main() {
    using namespace vice;

    world::World world;
    const world::RegionBounds bounds{
        .min = world::LocalPosition{0.0F, -100.0F, -100.0F},
        .max = world::LocalPosition{100.0F, 100.0F, 100.0F},
    };
    const world::RegionId region_a{1};
    const world::RegionId region_b{2};
    const world::RegionId region_c{3};

    assert(world.register_region(world::RegionDesc{
        .id = region_a,
        .simulation_island = world::SimulationIslandId{101},
        .origin = world::GlobalPosition{1'000'000'000.0, 0.0, 0.0},
        .streaming_cell = world::StreamingCellId{0, 0, 0},
        .fidelity = world::RegionFidelity::full,
        .bounds = bounds,
    }));
    assert(world.register_region(world::RegionDesc{
        .id = region_b,
        .simulation_island = world::SimulationIslandId{202},
        .origin = world::GlobalPosition{1'000'000'100.0, 0.0, 0.0},
        .streaming_cell = world::StreamingCellId{1, 0, 0},
        .fidelity = world::RegionFidelity::full,
        .bounds = bounds,
    }));
    assert(world.register_region(world::RegionDesc{
        .id = region_c,
        .simulation_island = world::SimulationIslandId{303},
        .origin = world::GlobalPosition{1'000'000'200.0, 0.0, 0.0},
        .streaming_cell = world::StreamingCellId{2, 0, 0},
        .fidelity = world::RegionFidelity::inactive,
        .bounds = bounds,
    }));

    auto domain_a = rigid_body::Domain::create(simulation::DomainId{10}, "auto.a");
    auto domain_b = rigid_body::Domain::create(simulation::DomainId{20}, "auto.b");
    auto domain_c = rigid_body::Domain::create(simulation::DomainId{30}, "auto.c");
    assert(domain_a != nullptr);
    assert(domain_b != nullptr);
    assert(domain_c != nullptr);

    simulation::Coordinator coordinator{1.0 / 120.0};
    assert(coordinator.register_domain(*domain_a));
    assert(coordinator.register_domain(*domain_b));

    rigid_body::WorldBridge bridge;
    assert(bridge.register_region(world, coordinator, region_a, *domain_a));
    assert(bridge.register_region(world, coordinator, region_b, *domain_b));

    const auto entity = world.create_entity();
    const auto owner = world.persistent_id(entity);
    assert(owner.has_value());
    assert(world.set_spatial_state(entity, world::SpatialState{
        .global_position = world::GlobalPosition{1'000'000'050.0, 0.0, 0.0},
        .region = region_a,
    }));

    rigid_body::CharacterDesc desc;
    desc.id = rigid_body::CharacterId{7};
    assert(bridge.create_character(world, coordinator, entity, desc));

    bridge.orchestrate_characters(world, coordinator);
    assert(bridge.character_region_events().size() == 1U);
    assert(bridge.character_region_events()[0].owner == *owner);
    assert(bridge.character_region_events()[0].source_region == region_a);
    assert(bridge.character_region_events()[0].action == rigid_body::CharacterRegionAction::stayed);
    assert(bridge.region_activation_requests().empty());
    assert(world.spatial_state(entity)->region == region_a);

    const auto first_id = bridge.character_id(world, coordinator, entity);
    assert(first_id.has_value());
    assert(domain_a->set_character_velocity(*first_id, math::Vec3{4.0F, 2.0F, 0.0F}));
    assert(domain_a->set_character_horizontal_velocity(*first_id, math::Vec3{3.0F, 0.0F, 0.0F}));
    assert(domain_a->set_character_position(*first_id, math::Vec3{105.0F, 4.0F, 0.0F}));

    bridge.orchestrate_characters(world, coordinator);
    assert(bridge.character_region_events().size() == 1U);
    assert(bridge.character_region_events()[0].action ==
        rigid_body::CharacterRegionAction::handed_off);
    assert(bridge.character_region_events()[0].target_region == region_b);
    assert(world.spatial_state(entity)->region == region_b);
    assert(near(world.spatial_state(entity)->global_position.x, 1'000'000'105.0));
    assert(domain_a->character_count() == 0U);
    assert(domain_b->character_count() == 1U);

    const auto second_id = bridge.character_id(world, coordinator, entity);
    assert(second_id.has_value());
    const auto second_state = domain_b->character_state(*second_id);
    assert(second_state.has_value());
    assert(near(second_state->position.x, 5.0F));
    assert(near(second_state->velocity.x, 4.0F));
    assert(near(second_state->horizontal_velocity.x, 3.0F));

    assert(domain_b->set_character_velocity(*second_id, math::Vec3{20.0F, 0.0F, 0.0F}));
    assert(domain_b->set_character_position(*second_id, math::Vec3{95.0F, 6.0F, 0.0F}));
    bridge.orchestrate_characters(world, coordinator);
    assert(bridge.character_region_events().size() == 1U);
    assert(bridge.character_region_events()[0].action ==
        rigid_body::CharacterRegionAction::activation_requested);
    assert(bridge.character_region_events()[0].target_region == region_c);
    assert(bridge.region_activation_requests().size() == 1U);
    assert(world.spatial_state(entity)->region == region_b);
    assert(near(world.spatial_state(entity)->global_position.x, 1'000'000'195.0));
    assert(domain_b->character_state(*second_id).has_value());

    const world::StreamingCellId expected_target_cell{2, 0, 0};
    assert(bridge.region_activation_requests()[0].owner == *owner);
    assert(bridge.region_activation_requests()[0].source_region == region_b);
    assert(bridge.region_activation_requests()[0].target_region == region_c);
    assert(bridge.region_activation_requests()[0].target_island ==
        world::SimulationIslandId{303});
    assert(bridge.region_activation_requests()[0].target_cell == expected_target_cell);

    assert(domain_b->set_character_position(*second_id, math::Vec3{110.0F, 6.0F, 0.0F}));
    bridge.orchestrate_characters(world, coordinator);
    assert(bridge.character_region_events().size() == 1U);
    assert(bridge.character_region_events()[0].action ==
        rigid_body::CharacterRegionAction::activation_requested);
    assert(bridge.character_region_events()[0].target_region == region_c);
    assert(bridge.region_activation_requests().size() == 1U);
    assert(world.spatial_state(entity)->region == region_b);
    assert(near(world.spatial_state(entity)->global_position.x, 1'000'000'210.0));
    assert(domain_b->character_state(*second_id).has_value());
    assert(domain_c->character_count() == 0U);

    bridge.orchestrate_characters(world, coordinator);
    assert(bridge.region_activation_requests().size() == 1U);
    assert(bridge.character_region_events()[0].action ==
        rigid_body::CharacterRegionAction::activation_requested);

    bridge.clear_region_activation_requests();
    assert(bridge.region_activation_requests().empty());
    bridge.orchestrate_characters(world, coordinator);
    assert(bridge.region_activation_requests().size() == 1U);

    assert(world.set_region_fidelity(region_c, world::RegionFidelity::full));
    assert(coordinator.register_domain(*domain_c));
    assert(bridge.register_region(world, coordinator, region_c, *domain_c));

    bridge.orchestrate_characters(world, coordinator);
    assert(bridge.character_region_events().size() == 1U);
    assert(bridge.character_region_events()[0].action ==
        rigid_body::CharacterRegionAction::handed_off);
    assert(bridge.character_region_events()[0].target_region == region_c);
    assert(bridge.region_activation_requests().empty());
    assert(world.spatial_state(entity)->region == region_c);
    assert(domain_b->character_count() == 0U);
    assert(domain_c->character_count() == 1U);

    const auto third_id = bridge.character_id(world, coordinator, entity);
    assert(third_id.has_value());
    const auto third_state = domain_c->character_state(*third_id);
    assert(third_state.has_value());
    assert(near(third_state->position.x, 10.0F));
    assert(near(third_state->position.y, 6.0F));
    assert(near(third_state->velocity.x, 20.0F));

    assert(domain_c->set_character_velocity(*third_id, math::Vec3{}));
    assert(domain_c->set_character_position(*third_id, math::Vec3{150.0F, 8.0F, 0.0F}));
    bridge.orchestrate_characters(world, coordinator);
    assert(bridge.character_region_events().size() == 1U);
    assert(bridge.character_region_events()[0].action ==
        rigid_body::CharacterRegionAction::no_target);
    assert(!bridge.character_region_events()[0].target_region.has_value());
    assert(bridge.region_activation_requests().empty());
    assert(world.spatial_state(entity)->region == region_c);
    assert(near(world.spatial_state(entity)->global_position.x, 1'000'000'350.0));
    assert(domain_c->character_state(*third_id).has_value());

    assert(domain_c->destroy_character(*third_id));
    assert(coordinator.unregister_domain(domain_a->id()));
    assert(coordinator.unregister_domain(domain_b->id()));
    assert(coordinator.unregister_domain(domain_c->id()));
    return 0;
}
