#include <vice/world/persistent_id.hpp>
#include <vice/world/relation.hpp>
#include <vice/world/reflection_probe.hpp>
#include <vice/world/snapshot_file.hpp>
#include <vice/world/spatial.hpp>
#include <vice/world/world.hpp>

#include <cassert>
#include <cmath>

namespace {

vice::world::PersistentId fixed_id(const std::uint8_t tail) {
    vice::world::PersistentId::Bytes bytes{};
    bytes[15] = tail;
    return vice::world::PersistentId{bytes};
}

bool near(const float left, const float right, const float tolerance = 0.0002F) {
    return std::abs(left - right) <= tolerance;
}

void test_persistent_id_round_trip() {
    using vice::world::PersistentId;

    const auto id = PersistentId::random();
    assert(!id.is_nil());

    const auto text = id.to_string();
    assert(text.size() == 36);

    const auto parsed = PersistentId::parse(text);
    assert(parsed.has_value());
    assert(*parsed == id);
    assert(!PersistentId::parse("not-an-id").has_value());
}

void test_entity_lifetime_and_stale_handles() {
    vice::world::World world;

    const auto first = world.create_entity();
    assert(world.alive(first));
    assert(world.entity_count() == 1);

    assert(world.destroy_entity(first));
    assert(!world.alive(first));
    assert(world.entity_count() == 0);
    assert(!world.destroy_entity(first));

    const auto second = world.create_entity();
    assert(second.index == first.index);
    assert(second.generation != first.generation);
    assert(world.alive(second));
    assert(!world.spatial_state(second).has_value());
    assert(!world.authored_transform(second).has_value());
    assert(world.resource_references(second).empty());
}

void test_persistent_identity_restore() {
    vice::world::World first_world;
    const auto id = fixed_id(7);

    const auto first_entity = first_world.create_entity(id);
    assert(first_entity.has_value());
    assert(!first_world.create_entity(id).has_value());

    const auto serialized = id.to_string();
    const auto restored_id = vice::world::PersistentId::parse(serialized);
    assert(restored_id.has_value());

    vice::world::World second_world;
    const auto second_entity = second_world.create_entity(*restored_id);
    assert(second_entity.has_value());
    assert(second_world.persistent_id(*second_entity) == restored_id);
    assert(second_world.find(*restored_id) == second_entity);
}

void test_relations() {
    vice::world::World world;
    const auto parent = world.create_entity();
    const auto child = world.create_entity();
    const auto relation_kind = fixed_id(42);

    const vice::world::Relation relation{child, relation_kind, parent};
    assert(world.add_relation(relation));
    assert(!world.add_relation(relation));
    assert(world.relations().size() == 1);

    assert(world.destroy_entity(parent));
    assert(world.relations().empty());
}

void test_resource_references() {
    using namespace vice::world;

    World world;
    const ResourceId mesh_resource{fixed_id(60)};
    const ResourceId missing_resource{fixed_id(61)};
    const auto resource = world.create_resource(mesh_resource);
    assert(resource.has_value());

    const auto entity_id = fixed_id(62);
    const auto entity = world.create_entity(entity_id);
    assert(entity.has_value());

    const ResourceReference missing_reference{
        .role = ResourceRole::render_mesh,
        .resource = missing_resource,
    };
    assert(!world.set_resource_reference(*entity, missing_reference));

    const ResourceReference mesh_reference{
        .role = ResourceRole::render_mesh,
        .resource = mesh_resource,
    };
    assert(world.set_resource_reference(*entity, mesh_reference));
    assert(world.resource_reference(*entity, ResourceRole::render_mesh) == std::optional{mesh_resource});
    assert(world.resource_references(*entity).size() == 1U);
    assert(!world.destroy_resource(*resource));

    const auto snapshot = world.snapshot();
    assert(snapshot.entities.size() == 1U);
    assert(snapshot.entities[0].resource_references.size() == 1U);
    assert(snapshot.entities[0].resource_references[0] == mesh_reference);

    World restored;
    assert(restored.restore(snapshot));
    const auto restored_entity = restored.find(entity_id);
    const auto restored_resource = restored.find_resource(mesh_resource);
    assert(restored_entity.has_value());
    assert(restored_resource.has_value());
    assert(restored.resource_reference(*restored_entity, ResourceRole::render_mesh) == std::optional{mesh_resource});
    assert(!restored.destroy_resource(*restored_resource));
    assert(restored.clear_resource_reference(*restored_entity, ResourceRole::render_mesh));
    assert(!restored.resource_reference(*restored_entity, ResourceRole::render_mesh).has_value());
    assert(restored.destroy_resource(*restored_resource));
}

void test_large_world_coordinate_conversion() {
    using namespace vice::world;

    const GlobalPosition origin{
        1'000'000'000.0,
        -2'000'000'000.0,
        500'000'000.0,
    };
    const GlobalPosition position{
        origin.x + 0.01,
        origin.y - 0.02,
        origin.z + 0.03,
    };

    const auto local = to_local_position(position, origin);
    assert(local.has_value());
    assert(near(local->x, 0.01F));
    assert(near(local->y, -0.02F));
    assert(near(local->z, 0.03F));

    const GlobalPosition camera{
        origin.x + 0.005,
        origin.y - 0.01,
        origin.z + 0.01,
    };
    const auto relative = camera_relative_position(position, camera);
    assert(relative.has_value());
    assert(near(relative->x, 0.005F));
    assert(near(relative->y, -0.01F));
    assert(near(relative->z, 0.02F));

    const LocalPosition solver_position{12.5F, -3.25F, 8.0F};
    const auto restored = to_global_position(solver_position, origin);
    assert(restored.has_value());
    const auto round_trip = to_local_position(*restored, origin);
    assert(round_trip.has_value());
    assert(near(round_trip->x, solver_position.x));
    assert(near(round_trip->y, solver_position.y));
    assert(near(round_trip->z, solver_position.z));
}

void test_region_registration_and_island_handoff() {
    using namespace vice::world;

    World world;
    const RegionDesc first_region{
        .id = RegionId{1},
        .simulation_island = SimulationIslandId{11},
        .origin = GlobalPosition{1'000'000'000.0, 0.0, -1'000'000'000.0},
        .streaming_cell = StreamingCellId{100, 0, -100},
        .fidelity = RegionFidelity::full,
    };
    const RegionDesc second_region{
        .id = RegionId{2},
        .simulation_island = SimulationIslandId{22},
        .origin = GlobalPosition{1'000'001'000.0, 0.0, -1'000'000'000.0},
        .streaming_cell = StreamingCellId{101, 0, -100},
        .fidelity = RegionFidelity::reduced,
    };

    assert(world.register_region(first_region));
    assert(world.register_region(second_region));
    assert(!world.register_region(first_region));
    assert(!world.register_region(RegionDesc{}));

    const auto entity = world.create_entity(fixed_id(90));
    assert(entity.has_value());
    const auto identity_before = world.persistent_id(*entity);
    assert(identity_before.has_value());

    const SpatialState state{
        .global_position = GlobalPosition{1'000'000'010.0, 2.0, -999'999'997.0},
        .region = first_region.id,
    };
    assert(world.set_spatial_state(*entity, state));

    const auto first_local = world.local_position(*entity);
    assert(first_local.has_value());
    assert(near(first_local->x, 10.0F));
    assert(near(first_local->y, 2.0F));
    assert(near(first_local->z, 3.0F));
    assert(world.simulation_island(*entity) == first_region.simulation_island);
    assert(world.streaming_cell(*entity) == first_region.streaming_cell);
    assert(!world.remove_region(first_region.id));

    assert(world.handoff_spatial(*entity, second_region.id));
    const auto state_after = world.spatial_state(*entity);
    assert(state_after.has_value());
    assert(state_after->global_position == state.global_position);
    assert(state_after->region == second_region.id);
    assert(world.persistent_id(*entity) == identity_before);
    assert(world.simulation_island(*entity) == second_region.simulation_island);
    assert(world.streaming_cell(*entity) == second_region.streaming_cell);

    const auto second_local = world.local_position(*entity);
    assert(second_local.has_value());
    assert(near(second_local->x, -990.0F));
    assert(near(second_local->y, 2.0F));
    assert(near(second_local->z, 3.0F));

    assert(world.set_local_position(*entity, LocalPosition{-5.0F, 4.0F, 7.0F}));
    const auto moved = world.spatial_state(*entity);
    assert(moved.has_value());
    assert((moved->global_position == GlobalPosition{1'000'000'995.0, 4.0, -999'999'993.0}));

    assert(world.set_region_fidelity(second_region.id, RegionFidelity::inactive));
    const auto updated_region = world.region(second_region.id);
    assert(updated_region.has_value());
    assert(updated_region->fidelity == RegionFidelity::inactive);

    const SpatialReference reference{
        .entity = *identity_before,
        .last_known_cell = second_region.streaming_cell,
    };
    assert(world.find(reference.entity) == entity);
    assert(reference.last_known_cell == second_region.streaming_cell);
}

void test_small_world_uses_same_spatial_api() {
    using namespace vice::world;

    World world;
    const RegionDesc region{
        .id = RegionId{1},
        .simulation_island = SimulationIslandId{1},
        .origin = GlobalPosition{},
        .streaming_cell = StreamingCellId{},
        .fidelity = RegionFidelity::full,
    };
    assert(world.register_region(region));

    const auto entity = world.create_entity();
    assert(world.set_spatial_state(entity, SpatialState{
        .global_position = GlobalPosition{2.0, 3.0, 4.0},
        .region = region.id,
    }));

    const auto local = world.local_position(entity);
    assert((local == LocalPosition{2.0F, 3.0F, 4.0F}));

    const auto state = world.spatial_state(entity);
    assert(state.has_value());
    const auto relative = camera_relative_position(
        state->global_position,
        GlobalPosition{1.0, 1.0, 1.0}
    );
    assert((relative == LocalPosition{1.0F, 2.0F, 3.0F}));

    assert(!world.remove_region(region.id));
    assert(world.clear_spatial_state(entity));
    assert(world.remove_region(region.id));
}

void test_authored_transform_state() {
    using namespace vice::world;

    World world;
    const RegionDesc region{
        .id = RegionId{1},
        .simulation_island = SimulationIslandId{1},
        .origin = GlobalPosition{},
        .streaming_cell = StreamingCellId{},
        .fidelity = RegionFidelity::full,
    };
    assert(world.register_region(region));

    const auto entity = world.create_entity();
    const AuthoredTransform quarter_turn{
        .rotation = {0.0F, 0.0F, 0.70710678F, 0.70710678F},
        .scale = {1.5F, 2.0F, 0.75F},
    };
    assert(!world.set_authored_transform(entity, quarter_turn));

    assert(world.set_spatial_state(entity, SpatialState{
        .global_position = {1.0, 2.0, 3.0},
        .region = region.id,
    }));
    assert(!world.set_authored_transform(entity, AuthoredTransform{
        .rotation = {0.0F, 0.0F, 0.0F, 2.0F},
    }));
    assert(!world.set_authored_transform(entity, AuthoredTransform{
        .scale = {1.0F, -1.0F, 1.0F},
    }));

    assert(world.set_authored_transform(entity, quarter_turn));
    assert(world.authored_transform(entity) == std::optional{quarter_turn});

    assert(world.handoff_spatial(entity, region.id));
    assert(world.authored_transform(entity) == std::optional{quarter_turn});

    assert(world.clear_authored_transform(entity));
    assert(!world.authored_transform(entity).has_value());
    assert(!world.clear_authored_transform(entity));

    assert(world.set_authored_transform(entity, quarter_turn));
    assert(world.clear_spatial_state(entity));
    assert(!world.authored_transform(entity).has_value());
}


void test_reflection_probe_state_and_snapshot() {
    using namespace vice::world;

    World world;
    const RegionDesc region{
        .id = RegionId{9},
        .simulation_island = SimulationIslandId{9},
        .origin = GlobalPosition{},
        .streaming_cell = StreamingCellId{},
        .fidelity = RegionFidelity::full,
    };
    assert(world.register_region(region));

    const ResourceId environment{fixed_id(110)};
    assert(world.create_resource(environment).has_value());
    const auto entity = world.create_entity(fixed_id(111));
    assert(entity.has_value());
    assert(world.set_spatial_state(*entity, SpatialState{
        .global_position = {20.0, 3.0, -4.0},
        .region = region.id,
    }));

    const ReflectionProbe probe{
        .half_extents = {8.0F, 4.0F, 6.0F},
        .blend_distance = 2.0F,
        .priority = 7,
        .enabled = true,
    };
    assert(world.set_reflection_probe(*entity, probe));
    assert(world.set_resource_reference(*entity, ResourceReference{
        .role = ResourceRole::reflection_environment,
        .resource = environment,
    }));
    assert(world.reflection_probe(*entity) == std::optional{probe});

    const auto encoded = encode_world_snapshot(world.snapshot());
    assert(encoded);
    const auto decoded = decode_world_snapshot(encoded.bytes);
    assert(decoded.snapshot.has_value());

    World restored;
    assert(restored.restore(*decoded.snapshot));
    const auto restored_entity = restored.find(fixed_id(111));
    assert(restored_entity.has_value());
    assert(restored.reflection_probe(*restored_entity) == std::optional{probe});
    assert(restored.resource_reference(*restored_entity, ResourceRole::reflection_environment) ==
        std::optional{environment});

    assert(restored.clear_spatial_state(*restored_entity));
    assert(!restored.reflection_probe(*restored_entity).has_value());
}

} // namespace

int main() {
    test_persistent_id_round_trip();
    test_entity_lifetime_and_stale_handles();
    test_persistent_identity_restore();
    test_relations();
    test_resource_references();
    test_large_world_coordinate_conversion();
    test_region_registration_and_island_handoff();
    test_small_world_uses_same_spatial_api();
    test_authored_transform_state();
    test_reflection_probe_state_and_snapshot();
    return 0;
}
