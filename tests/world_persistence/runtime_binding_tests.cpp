#include <vice/rigid_body/world_bridge.hpp>
#include <vice/world/snapshot_file.hpp>
#include <vice/world/world.hpp>

#include <cassert>

int main() {
    using namespace vice;

    const auto player_id = world::PersistentId::parse(
        "10000000-0000-4000-8000-000000000001"
    );
    const auto cube_id = world::PersistentId::parse(
        "10000000-0000-4000-8000-000000000002"
    );
    assert(player_id.has_value());
    assert(cube_id.has_value());

    world::World source;
    const world::RegionBounds bounds{
        .min = world::LocalPosition{0.0F, -100.0F, -100.0F},
        .max = world::LocalPosition{8.0F, 100.0F, 100.0F},
    };
    assert(source.register_region(world::RegionDesc{
        .id = world::RegionId{1},
        .simulation_island = world::SimulationIslandId{101},
        .origin = world::GlobalPosition{-8.0, 0.0, 0.0},
        .streaming_cell = world::StreamingCellId{0, 0, 0},
        .fidelity = world::RegionFidelity::full,
        .bounds = bounds,
    }));
    assert(source.register_region(world::RegionDesc{
        .id = world::RegionId{2},
        .simulation_island = world::SimulationIslandId{202},
        .origin = world::GlobalPosition{0.0, 0.0, 0.0},
        .streaming_cell = world::StreamingCellId{1, 0, 0},
        .fidelity = world::RegionFidelity::full,
        .bounds = bounds,
    }));

    const auto player = source.create_entity(*player_id);
    const auto cube = source.create_entity(*cube_id);
    assert(player.has_value());
    assert(cube.has_value());
    assert(source.set_spatial_state(*player, world::SpatialState{
        .global_position = world::GlobalPosition{-4.0, 0.0, 0.0},
        .region = world::RegionId{1},
    }));
    assert(source.set_spatial_state(*cube, world::SpatialState{
        .global_position = world::GlobalPosition{-2.5, 2.2, -2.0},
        .region = world::RegionId{1},
    }));

    const auto encoded = world::encode_world_snapshot(source.snapshot());
    assert(encoded);
    const auto decoded = world::decode_world_snapshot(encoded.bytes);
    assert(decoded.snapshot.has_value());

    world::World restored;
    assert(restored.restore(*decoded.snapshot));
    const auto restored_player = restored.find(*player_id);
    const auto restored_cube = restored.find(*cube_id);
    assert(restored_player.has_value());
    assert(restored_cube.has_value());

    auto domain_a = rigid_body::Domain::create(simulation::DomainId{100}, "restored.a");
    auto domain_b = rigid_body::Domain::create(simulation::DomainId{200}, "restored.b");
    assert(domain_a != nullptr);
    assert(domain_b != nullptr);

    simulation::Coordinator coordinator{1.0 / 120.0};
    assert(coordinator.register_domain(*domain_a));
    assert(coordinator.register_domain(*domain_b));

    rigid_body::WorldBridge bridge;
    assert(bridge.register_region(restored, coordinator, world::RegionId{1}, *domain_a));
    assert(bridge.register_region(restored, coordinator, world::RegionId{2}, *domain_b));

    rigid_body::BoxBodyDesc floor;
    floor.participant = simulation::ParticipantId{10};
    floor.motion = rigid_body::MotionType::static_body;
    floor.half_extents = math::Vec3{4.0F, 0.25F, 20.0F};
    floor.position = math::Vec3{4.0F, -0.25F, 0.0F};
    assert(domain_a->create_box(floor));
    assert(domain_b->create_box(floor));

    simulation::EntityBindings bindings;
    rigid_body::BoxBodyDesc cube_body;
    cube_body.participant = simulation::ParticipantId{20};
    cube_body.motion = rigid_body::MotionType::dynamic_body;
    cube_body.half_extents = math::Vec3{0.7F, 0.7F, 0.7F};
    assert(bridge.create_box(
        restored,
        coordinator,
        bindings,
        *restored_cube,
        cube_body
    ));
    assert(bindings.find(
        restored,
        coordinator,
        *restored_cube,
        domain_a->id()
    ) == simulation::ParticipantId{20});

    rigid_body::CharacterDesc character;
    character.id = rigid_body::CharacterId{1};
    assert(bridge.create_character(restored, coordinator, *restored_player, character));
    assert(bridge.character_id(restored, coordinator, *restored_player).has_value());

    coordinator.step();
    bridge.orchestrate_characters(restored, coordinator);
    assert(bridge.sync_body_to_world(restored, coordinator, bindings, *restored_cube));
    assert(bridge.sync_character_to_world(restored, coordinator, *restored_player));

    const auto persisted = world::encode_world_snapshot(restored.snapshot());
    assert(persisted);
    const auto persisted_decode = world::decode_world_snapshot(persisted.bytes);
    assert(persisted_decode.snapshot.has_value());
    world::World second_restore;
    assert(second_restore.restore(*persisted_decode.snapshot));
    assert(second_restore.find(*player_id).has_value());
    assert(second_restore.find(*cube_id).has_value());
    assert(second_restore.spatial_state(*second_restore.find(*player_id))->region ==
        restored.spatial_state(*restored_player)->region);
    assert(second_restore.spatial_state(*second_restore.find(*cube_id))->region ==
        restored.spatial_state(*restored_cube)->region);

    return 0;
}
