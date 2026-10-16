#include <vice/rigid_body/world_bridge.hpp>

#include <cassert>
#include <cmath>

namespace {

vice::world::PersistentId fixed_id(const std::uint8_t tail) {
    vice::world::PersistentId::Bytes bytes{};
    bytes[15] = tail;
    return vice::world::PersistentId{bytes};
}

bool near(const float left, const float right, const float tolerance = 0.02F) {
    return std::abs(left - right) <= tolerance;
}

} // namespace

int main() {
    using namespace vice;

    world::World world;
    const world::RegionId region_a{1};
    const world::RegionId region_b{2};
    const world::RegionBounds bounds{
        .min = world::LocalPosition{0.0F, -100.0F, -100.0F},
        .max = world::LocalPosition{10.0F, 100.0F, 100.0F},
    };

    assert(world.register_region(world::RegionDesc{
        .id = region_a,
        .simulation_island = world::SimulationIslandId{101},
        .origin = world::GlobalPosition{0.0, 0.0, 0.0},
        .streaming_cell = world::StreamingCellId{0, 0, 0},
        .fidelity = world::RegionFidelity::full,
        .bounds = bounds,
    }));
    assert(world.register_region(world::RegionDesc{
        .id = region_b,
        .simulation_island = world::SimulationIslandId{202},
        .origin = world::GlobalPosition{10.0, 0.0, 0.0},
        .streaming_cell = world::StreamingCellId{1, 0, 0},
        .fidelity = world::RegionFidelity::inactive,
        .bounds = bounds,
    }));

    auto domain_a = rigid_body::Domain::create(simulation::DomainId{10}, "rigid.a");
    auto domain_b = rigid_body::Domain::create(simulation::DomainId{20}, "rigid.b");
    assert(domain_a != nullptr);
    assert(domain_b != nullptr);

    simulation::Coordinator coordinator{1.0 / 120.0};
    assert(coordinator.register_domain(*domain_a));

    rigid_body::WorldBridge bridge;
    assert(bridge.register_region(world, coordinator, region_a, *domain_a));
    assert(bridge.registered_region_count() == 1U);
    assert(!coordinator.registered(domain_b->id()));

    const auto entity = world.create_entity(fixed_id(42));
    assert(entity.has_value());
    assert(world.set_spatial_state(*entity, world::SpatialState{
        .global_position = world::GlobalPosition{8.5, 2.0, 0.0},
        .region = region_a,
    }));

    simulation::EntityBindings bindings;
    const simulation::ParticipantId participant{77};
    rigid_body::BoxBodyDesc box;
    box.participant = participant;
    box.motion = rigid_body::MotionType::dynamic_body;
    box.motion_quality = rigid_body::MotionQuality::continuous;
    box.half_extents = math::Vec3{0.5F, 0.5F, 0.5F};
    box.orientation = math::Quat{0.0F, 0.0F, 0.2588190451F, 0.9659258263F};
    box.linear_velocity = math::Vec3{4.0F, 0.0F, 0.0F};
    box.angular_velocity = math::Vec3{0.0F, 1.0F, 0.25F};
    box.friction = 0.2F;
    box.restitution = 0.1F;
    assert(bridge.create_box(world, coordinator, bindings, *entity, box));
    assert(bridge.box_count() == 1U);

    bridge.orchestrate_boxes(
        world,
        coordinator,
        bindings,
        rigid_body::BoxRegionOrchestrationConfig{.prefetch_seconds = 0.5F}
    );
    assert(bridge.box_region_events().size() == 1U);
    assert(bridge.box_region_events().front().action ==
        rigid_body::BoxRegionAction::activation_requested);
    assert(bridge.box_region_events().front().target_region == region_b);
    assert(bridge.region_activation_requests().size() == 1U);
    assert(bridge.region_activation_requests().front().owner == fixed_id(42));
    assert(bridge.region_activation_requests().front().target_region == region_b);
    assert(bridge.region_activation_requests().front().target_island ==
        world::SimulationIslandId{202});
    assert(bridge.region_activation_requests().front().target_cell ==
        (world::StreamingCellId{1, 0, 0}));

    for (int step = 0; step < 60; ++step) {
        coordinator.step();
    }

    bridge.orchestrate_boxes(world, coordinator, bindings);
    assert(bridge.box_region_events().size() == 1U);
    assert(bridge.box_region_events().front().action ==
        rigid_body::BoxRegionAction::activation_requested);
    assert(domain_a->body_state(participant).has_value());
    assert(!domain_b->body_state(participant).has_value());
    assert(world.spatial_state(*entity)->region == region_a);
    assert(world.spatial_state(*entity)->global_position.x > 10.0);

    const auto source_state = domain_a->body_state(participant);
    assert(source_state.has_value());
    assert(source_state->active);
    assert(source_state->linear_velocity.x > 3.0F);

    assert(bridge.activate_region(world, coordinator, region_b, *domain_b));
    assert(coordinator.registered(domain_b->id()));
    assert(bridge.registered_region_count() == 2U);
    assert(world.region(region_b)->fidelity == world::RegionFidelity::full);
    assert(domain_b->fidelity() == simulation::Fidelity::full);

    bridge.orchestrate_boxes(world, coordinator, bindings);
    assert(bridge.box_region_events().size() == 1U);
    assert(bridge.box_region_events().front().action == rigid_body::BoxRegionAction::handed_off);
    assert(bridge.box_region_events().front().source_region == region_a);
    assert(bridge.box_region_events().front().target_region == region_b);
    assert(bridge.region_activation_requests().empty());

    const auto transferred_spatial = world.spatial_state(*entity);
    assert(transferred_spatial.has_value());
    assert(transferred_spatial->region == region_b);
    assert(transferred_spatial->global_position.x > 10.0);
    assert(!domain_a->body_state(participant).has_value());

    const auto target_state = domain_b->body_state(participant);
    assert(target_state.has_value());
    assert(target_state->active == source_state->active);
    assert(target_state->motion_quality == source_state->motion_quality);
    assert(near(target_state->linear_velocity.x, source_state->linear_velocity.x));
    assert(near(target_state->angular_velocity.y, source_state->angular_velocity.y));
    assert(near(target_state->orientation.z, source_state->orientation.z));
    assert(near(target_state->orientation.w, source_state->orientation.w));
    assert(bindings.find(world, coordinator, *entity, domain_b->id()) == participant);
    assert(!bindings.find(world, coordinator, *entity, domain_a->id()).has_value());

    assert(bridge.activate_region(world, coordinator, region_b, *domain_b));
    assert(bridge.registered_region_count() == 2U);

    return 0;
}
