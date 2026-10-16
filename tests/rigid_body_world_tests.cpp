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
    const world::SimulationIslandId island_a{101};
    const world::SimulationIslandId island_b{202};

    assert(world.register_region(world::RegionDesc{
        .id = region_a,
        .simulation_island = island_a,
        .origin = world::GlobalPosition{1'000'000'000.0, 0.0, 0.0},
        .streaming_cell = world::StreamingCellId{0, 0, 0},
        .fidelity = world::RegionFidelity::full,
    }));
    assert(world.register_region(world::RegionDesc{
        .id = region_b,
        .simulation_island = island_b,
        .origin = world::GlobalPosition{1'000'001'000.0, 0.0, 0.0},
        .streaming_cell = world::StreamingCellId{1, 0, 0},
        .fidelity = world::RegionFidelity::full,
    }));

    auto domain_a = rigid_body::Domain::create(simulation::DomainId{10}, "rigid.a");
    auto domain_b = rigid_body::Domain::create(simulation::DomainId{20}, "rigid.b");
    assert(domain_a != nullptr);
    assert(domain_b != nullptr);

    simulation::Coordinator coordinator{1.0 / 120.0};
    assert(coordinator.register_domain(*domain_a));
    assert(coordinator.register_domain(*domain_b));

    rigid_body::WorldBridge bridge;
    assert(bridge.register_region(world, coordinator, region_a, *domain_a));
    assert(bridge.register_region(world, coordinator, region_b, *domain_b));
    assert(bridge.registered_region_count() == 2U);
    assert(!bridge.register_region(world, coordinator, region_a, *domain_b));

    const auto entity = world.create_entity();
    const auto persistent_id = world.persistent_id(entity);
    assert(persistent_id.has_value());
    assert(world.set_spatial_state(entity, world::SpatialState{
        .global_position = world::GlobalPosition{1'000'000'010.0, 8.0, 0.0},
        .region = region_a,
    }));

    simulation::EntityBindings bindings;
    const simulation::ParticipantId participant{77};

    rigid_body::BoxBodyDesc box;
    box.participant = participant;
    box.motion = rigid_body::MotionType::dynamic_body;
    box.half_extents = math::Vec3{0.5F, 0.75F, 1.0F};
    box.orientation = math::Quat{0.0F, 0.0F, 0.2588190451F, 0.9659258263F};
    box.linear_velocity = math::Vec3{5.0F, 0.0F, 0.0F};
    box.angular_velocity = math::Vec3{0.0F, 1.5F, 0.25F};
    box.friction = 0.63F;
    box.restitution = 0.21F;

    assert(bridge.create_box(world, coordinator, bindings, entity, box));
    assert(domain_a->body_count() == 1U);
    assert(domain_b->body_count() == 0U);
    assert(bindings.find(world, coordinator, entity, domain_a->id()) == participant);

    const auto initial = domain_a->body_state(participant);
    assert(initial.has_value());
    assert(near(initial->position.x, 10.0F));
    assert(near(initial->position.y, 8.0F));
    assert(near(initial->linear_velocity.x, 5.0F));
    assert(near(initial->angular_velocity.y, 1.5F));

    for (int step = 0; step < 60; ++step) {
        coordinator.step();
    }

    const auto live_source = domain_a->body_state(participant);
    assert(live_source.has_value());
    assert(live_source->active);
    assert(live_source->position.x > 12.0F);
    assert(live_source->linear_velocity.x > 1.0F);
    assert(std::abs(live_source->angular_velocity.y) > 0.1F);
    assert(std::isfinite(live_source->orientation.x));

    const auto blocker = world.create_entity();
    assert(bindings.bind(world, coordinator, blocker, domain_b->id(), participant));

    const auto spatial_before_failed_handoff = world.spatial_state(entity);
    assert(spatial_before_failed_handoff.has_value());
    assert(!bridge.handoff_box(world, coordinator, bindings, entity, region_b));
    assert(domain_a->body_state(participant).has_value());
    assert(!domain_b->body_state(participant).has_value());
    assert(bindings.find(world, coordinator, entity, domain_a->id()) == participant);
    assert(!bindings.find(world, coordinator, entity, domain_b->id()).has_value());
    const auto spatial_after_failed_handoff = world.spatial_state(entity);
    assert(spatial_after_failed_handoff.has_value());
    assert(spatial_after_failed_handoff->region == spatial_before_failed_handoff->region);
    assert(spatial_after_failed_handoff->global_position ==
        spatial_before_failed_handoff->global_position);
    assert(bindings.unbind(blocker, domain_b->id()));

    const double expected_global_x = 1'000'000'000.0 +
        static_cast<double>(live_source->position.x);
    const double expected_global_y = static_cast<double>(live_source->position.y);

    assert(bridge.handoff_box(world, coordinator, bindings, entity, region_b));

    assert(world.alive(entity));
    assert(world.persistent_id(entity) == persistent_id);
    const auto transferred_spatial = world.spatial_state(entity);
    assert(transferred_spatial.has_value());
    assert(transferred_spatial->region == region_b);
    assert(near(transferred_spatial->global_position.x, expected_global_x));
    assert(near(transferred_spatial->global_position.y, expected_global_y));

    assert(domain_a->body_count() == 0U);
    assert(domain_b->body_count() == 1U);
    assert(!bindings.find(world, coordinator, entity, domain_a->id()).has_value());
    assert(bindings.find(world, coordinator, entity, domain_b->id()) == participant);

    const auto transferred = domain_b->body_state(participant);
    assert(transferred.has_value());
    assert(near(
        transferred->position.x,
        static_cast<float>(expected_global_x - 1'000'001'000.0)
    ));
    assert(near(transferred->position.y, live_source->position.y));
    assert(near(transferred->linear_velocity.x, live_source->linear_velocity.x));
    assert(near(transferred->angular_velocity.y, live_source->angular_velocity.y));
    assert(transferred->active);

    for (int step = 0; step < 12; ++step) {
        coordinator.step();
    }

    const auto after_target_steps = domain_b->body_state(participant);
    assert(after_target_steps.has_value());
    assert(after_target_steps->position.x > transferred->position.x);

    assert(bridge.sync_body_to_world(world, coordinator, bindings, entity));
    const auto synced_spatial = world.spatial_state(entity);
    assert(synced_spatial.has_value());
    assert(near(
        synced_spatial->global_position.x,
        1'000'001'000.0 + static_cast<double>(after_target_steps->position.x)
    ));
    assert(near(
        synced_spatial->global_position.y,
        static_cast<double>(after_target_steps->position.y)
    ));

    assert(domain_b->set_body_active(participant, false));
    const auto inactive_source = domain_b->body_state(participant);
    assert(inactive_source.has_value());
    assert(!inactive_source->active);

    assert(bridge.handoff_box(world, coordinator, bindings, entity, region_a));
    assert(world.alive(entity));
    assert(world.persistent_id(entity) == persistent_id);
    assert(world.spatial_state(entity)->region == region_a);
    assert(domain_b->body_count() == 0U);
    assert(domain_a->body_count() == 1U);
    assert(!bindings.find(world, coordinator, entity, domain_b->id()).has_value());
    assert(bindings.find(world, coordinator, entity, domain_a->id()) == participant);

    const auto transferred_back = domain_a->body_state(participant);
    assert(transferred_back.has_value());
    assert(!transferred_back->active);
    assert(near(transferred_back->linear_velocity.x, inactive_source->linear_velocity.x));
    assert(near(transferred_back->angular_velocity.y, inactive_source->angular_velocity.y));

    assert(bindings.unbind(entity, domain_a->id()));
    assert(domain_a->destroy_body(participant));
    assert(coordinator.unregister_domain(domain_a->id()));
    assert(coordinator.unregister_domain(domain_b->id()));

    return 0;
}
