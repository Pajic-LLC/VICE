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
    const world::RegionId region_a{1};
    const world::RegionId region_b{2};

    assert(world.register_region(world::RegionDesc{
        .id = region_a,
        .simulation_island = world::SimulationIslandId{101},
        .origin = world::GlobalPosition{1'000'000'000.0, 0.0, 0.0},
        .streaming_cell = world::StreamingCellId{0, 0, 0},
        .fidelity = world::RegionFidelity::full,
    }));
    assert(world.register_region(world::RegionDesc{
        .id = region_b,
        .simulation_island = world::SimulationIslandId{202},
        .origin = world::GlobalPosition{1'000'001'000.0, 0.0, 0.0},
        .streaming_cell = world::StreamingCellId{1, 0, 0},
        .fidelity = world::RegionFidelity::full,
    }));

    auto domain_a = rigid_body::Domain::create(simulation::DomainId{10}, "character.world.a");
    auto domain_b = rigid_body::Domain::create(simulation::DomainId{20}, "character.world.b");
    assert(domain_a != nullptr);
    assert(domain_b != nullptr);

    simulation::Coordinator coordinator{1.0 / 120.0};
    assert(coordinator.register_domain(*domain_a));
    assert(coordinator.register_domain(*domain_b));

    rigid_body::WorldBridge bridge;
    assert(bridge.register_region(world, coordinator, region_a, *domain_a));
    assert(bridge.register_region(world, coordinator, region_b, *domain_b));

    const auto entity = world.create_entity();
    const auto persistent = world.persistent_id(entity);
    assert(persistent.has_value());
    assert(world.set_spatial_state(entity, world::SpatialState{
        .global_position = world::GlobalPosition{1'000'000'010.0, 5.0, 0.0},
        .region = region_a,
    }));

    rigid_body::CharacterDesc character;
    character.id = rigid_body::CharacterId{7};
    character.height = 1.9F;
    character.radius = 0.35F;
    character.mass = 82.0F;
    character.max_strength = 350.0F;
    character.max_slope_angle = 47.0F * rigid_body::pi / 180.0F;
    character.step_height = 0.45F;
    character.stick_to_floor_distance = 0.35F;
    character.collision.include_static = true;
    character.collision.include_dynamic = true;
    character.collision.include_triggers = false;

    assert(bridge.create_character(world, coordinator, entity, character));
    assert(!bridge.create_character(world, coordinator, entity, character));
    assert(bridge.character_count() == 1U);
    assert(bridge.character_id(world, coordinator, entity) == rigid_body::CharacterId{7});
    assert(domain_a->character_count() == 1U);

    const rigid_body::CharacterId source_id{7};
    assert(domain_a->set_character_velocity(source_id, math::Vec3{3.0F, 4.0F, -1.0F}));
    assert(domain_a->set_character_horizontal_velocity(
        source_id,
        math::Vec3{2.0F, 0.0F, -0.5F}
    ));
    assert(domain_a->request_character_jump(source_id, 6.0F));

    const auto source_state = domain_a->character_state(source_id);
    assert(source_state.has_value());
    assert(near(source_state->position.x, 10.0F));
    assert(near(source_state->position.y, 5.0F));
    assert(near(source_state->velocity.x, 3.0F));
    assert(near(source_state->velocity.y, 4.0F));
    assert(near(source_state->horizontal_velocity.x, 2.0F));
    assert(source_state->pending_jump_speed.has_value());
    assert(near(*source_state->pending_jump_speed, 6.0F));

    rigid_body::CharacterDesc blocker;
    blocker.id = source_id;
    blocker.position = math::Vec3{100.0F, 0.0F, 0.0F};
    assert(domain_b->create_character(blocker));

    assert(bridge.handoff_character(world, coordinator, entity, region_b));
    assert(world.alive(entity));
    assert(world.persistent_id(entity) == persistent);
    assert(world.spatial_state(entity)->region == region_b);
    assert(near(world.spatial_state(entity)->global_position.x, 1'000'000'010.0));
    assert(near(world.spatial_state(entity)->global_position.y, 5.0));

    const auto target_id = bridge.character_id(world, coordinator, entity);
    assert(target_id.has_value());
    assert(*target_id == rigid_body::CharacterId{8});
    assert(!domain_a->character_state(source_id).has_value());
    assert(domain_b->character_count() == 2U);

    const auto target_state = domain_b->character_state(*target_id);
    assert(target_state.has_value());
    assert(near(target_state->position.x, -990.0F));
    assert(near(target_state->position.y, 5.0F));
    assert(near(target_state->velocity.x, source_state->velocity.x));
    assert(near(target_state->velocity.y, source_state->velocity.y));
    assert(near(target_state->velocity.z, source_state->velocity.z));
    assert(near(target_state->horizontal_velocity.x, source_state->horizontal_velocity.x));
    assert(near(target_state->horizontal_velocity.z, source_state->horizontal_velocity.z));
    assert(target_state->pending_jump_speed == source_state->pending_jump_speed);

    const auto before_failed_handoff = world.spatial_state(entity);
    assert(before_failed_handoff.has_value());
    assert(coordinator.unregister_domain(domain_a->id()));
    assert(!bridge.handoff_character(world, coordinator, entity, region_a));
    assert(world.spatial_state(entity) == before_failed_handoff);
    assert(domain_b->character_state(*target_id).has_value());
    assert(bridge.character_id(world, coordinator, entity) == target_id);
    assert(coordinator.register_domain(*domain_a));

    assert(domain_b->set_character_position(*target_id, math::Vec3{-980.0F, 7.0F, 0.0F}));
    assert(bridge.sync_character_to_world(world, coordinator, entity));
    const auto synced = world.spatial_state(entity);
    assert(synced.has_value());
    assert(synced->region == region_b);
    assert(near(synced->global_position.x, 1'000'000'020.0));
    assert(near(synced->global_position.y, 7.0));

    coordinator.step();
    const auto after_step = domain_b->character_state(*target_id);
    assert(after_step.has_value());
    assert(after_step->position.x > -980.0F);
    assert(after_step->velocity.y < target_state->velocity.y);
    assert(!after_step->pending_jump_speed.has_value());

    assert(bridge.handoff_character(world, coordinator, entity, region_a));
    const auto return_id = bridge.character_id(world, coordinator, entity);
    assert(return_id.has_value());
    assert(domain_a->character_state(*return_id).has_value());
    assert(!domain_b->character_state(*target_id).has_value());
    assert(world.persistent_id(entity) == persistent);

    const auto returned_state = domain_a->character_state(*return_id);
    assert(returned_state.has_value());
    assert(near(returned_state->horizontal_velocity.x, after_step->horizontal_velocity.x));
    assert(near(returned_state->horizontal_velocity.z, after_step->horizontal_velocity.z));

    const auto persistent_before_rebind = *persistent;
    const auto spatial_before_rebind = *world.spatial_state(entity);
    assert(world.destroy_entity(entity));
    const auto rebound = world.create_entity(persistent_before_rebind);
    assert(rebound.has_value());
    assert(world.set_spatial_state(*rebound, spatial_before_rebind));
    assert(bridge.character_id(world, coordinator, *rebound) == return_id);
    assert(bridge.sync_character_to_world(world, coordinator, *rebound));

    assert(domain_a->destroy_character(*return_id));
    assert(domain_b->destroy_character(blocker.id));
    assert(coordinator.unregister_domain(domain_a->id()));
    assert(coordinator.unregister_domain(domain_b->id()));
    return 0;
}
