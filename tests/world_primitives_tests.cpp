#include <vice/world/component.hpp>
#include <vice/world/resource.hpp>
#include <vice/world/snapshot.hpp>
#include <vice/world/world.hpp>

#include <cassert>
#include <cstdint>

namespace {

vice::world::PersistentId fixed_id(const std::uint8_t tail) {
    vice::world::PersistentId::Bytes bytes{};
    bytes[15] = tail;
    return vice::world::PersistentId{bytes};
}

void test_component_registration() {
    using namespace vice::world;

    World world;
    const ComponentTypeDesc transform{
        .id = ComponentTypeId{fixed_id(1)},
        .name = "transform",
        .version = 2,
    };
    assert(world.register_component_type(transform));
    assert(!world.register_component_type(transform));
    assert(world.component_type_count() == 1U);
    assert(world.component_type(transform.id) == transform);

    assert(!world.register_component_type(ComponentTypeDesc{
        .id = ComponentTypeId{fixed_id(2)},
        .name = "transform",
        .version = 1,
    }));
    assert(!world.register_component_type(ComponentTypeDesc{
        .id = ComponentTypeId{fixed_id(3)},
        .name = "invalid",
        .version = 0,
    }));
    assert(!world.register_component_type(ComponentTypeDesc{}));

    assert(world.unregister_component_type(transform.id));
    assert(!world.unregister_component_type(transform.id));
    assert(world.component_type_count() == 0U);
}

void test_resource_handles_and_stale_generations() {
    using namespace vice::world;

    World world;
    const ResourceId first_id{fixed_id(10)};
    const auto first = world.create_resource(first_id);
    assert(first.has_value());
    assert(world.resource_alive(*first));
    assert(world.resource_count() == 1U);
    assert(world.resource_id(*first) == first_id);
    assert(world.find_resource(first_id) == first);
    assert(!world.create_resource(first_id).has_value());

    assert(world.destroy_resource(*first));
    assert(!world.resource_alive(*first));
    assert(!world.resource_id(*first).has_value());
    assert(!world.find_resource(first_id).has_value());
    assert(!world.destroy_resource(*first));

    const ResourceId second_id{fixed_id(11)};
    const auto second = world.create_resource(second_id);
    assert(second.has_value());
    assert(second->index == first->index);
    assert(second->generation != first->generation);
    assert(world.resource_alive(*second));
    assert(!world.resource_alive(*first));

    const auto random_resource = world.create_resource();
    assert(random_resource.valid());
    assert(world.resource_alive(random_resource));
    assert(world.resource_count() == 2U);
}

vice::world::WorldSnapshot make_snapshot(vice::world::ResourceHandle& saved_resource_handle) {
    using namespace vice::world;

    World world;
    assert(world.register_component_type(ComponentTypeDesc{
        .id = ComponentTypeId{fixed_id(12)},
        .name = "renderable",
        .version = 1,
    }));
    assert(world.register_component_type(ComponentTypeDesc{
        .id = ComponentTypeId{fixed_id(11)},
        .name = "transform",
        .version = 3,
    }));

    const auto discarded = world.create_resource(ResourceId{fixed_id(20)});
    assert(discarded.has_value());
    assert(world.destroy_resource(*discarded));
    const auto saved_resource = world.create_resource(ResourceId{fixed_id(21)});
    assert(saved_resource.has_value());
    saved_resource_handle = *saved_resource;
    assert(saved_resource_handle.generation > 1U);

    const RegionDesc second_region{
        .id = RegionId{2},
        .simulation_island = SimulationIslandId{22},
        .origin = GlobalPosition{1'000.0, 0.0, 0.0},
        .streaming_cell = StreamingCellId{2, 0, 0},
        .fidelity = RegionFidelity::reduced,
    };
    const RegionDesc first_region{
        .id = RegionId{1},
        .simulation_island = SimulationIslandId{11},
        .origin = GlobalPosition{},
        .streaming_cell = StreamingCellId{1, 0, 0},
        .fidelity = RegionFidelity::full,
    };
    assert(world.register_region(second_region));
    assert(world.register_region(first_region));

    const auto parent = world.create_entity(fixed_id(40));
    const auto child = world.create_entity(fixed_id(30));
    assert(parent.has_value());
    assert(child.has_value());
    assert(world.set_spatial_state(*child, SpatialState{
        .global_position = GlobalPosition{1'010.0, 2.0, 3.0},
        .region = second_region.id,
    }));
    assert(world.add_relation(Relation{*child, fixed_id(50), *parent}));

    return world.snapshot();
}

void test_snapshot_restore_and_transactionality() {
    using namespace vice::world;

    ResourceHandle old_resource_handle;
    const WorldSnapshot saved = make_snapshot(old_resource_handle);

    assert(saved.component_types.size() == 2U);
    assert(saved.component_types[0].id == ComponentTypeId{fixed_id(11)});
    assert(saved.component_types[1].id == ComponentTypeId{fixed_id(12)});
    assert(saved.resources.size() == 1U);
    assert(saved.resources[0] == ResourceId{fixed_id(21)});
    assert(saved.regions.size() == 2U);
    assert(saved.regions[0].id == RegionId{1});
    assert(saved.regions[1].id == RegionId{2});
    assert(saved.entities.size() == 2U);
    assert(saved.entities[0].persistent_id == fixed_id(30));
    assert(saved.entities[1].persistent_id == fixed_id(40));
    assert(saved.relations.size() == 1U);
    assert(saved.relations[0].subject == fixed_id(30));
    assert(saved.relations[0].kind == fixed_id(50));
    assert(saved.relations[0].object == fixed_id(40));

    World restored;
    const auto replaced_entity = restored.create_entity(fixed_id(99));
    assert(replaced_entity.has_value());
    assert(restored.restore(saved));
    assert(!restored.find(fixed_id(99)).has_value());

    assert(restored.entity_count() == 2U);
    assert(restored.component_type_count() == 2U);
    assert(restored.resource_count() == 1U);
    assert(restored.component_type(ComponentTypeId{fixed_id(11)})->version == 3U);

    const auto restored_resource = restored.find_resource(ResourceId{fixed_id(21)});
    assert(restored_resource.has_value());
    assert(restored.resource_alive(*restored_resource));
    assert(!restored.resource_alive(old_resource_handle));
    assert(restored_resource->generation == 1U);

    const auto child = restored.find(fixed_id(30));
    const auto parent = restored.find(fixed_id(40));
    assert(child.has_value());
    assert(parent.has_value());
    const auto child_state = restored.spatial_state(*child);
    assert(child_state.has_value());
    assert(child_state->region == RegionId{2});
    assert((child_state->global_position == GlobalPosition{1'010.0, 2.0, 3.0}));
    assert(restored.relations().size() == 1U);
    assert((restored.relations()[0] == Relation{*child, fixed_id(50), *parent}));

    const WorldSnapshot round_trip = restored.snapshot();
    assert(round_trip.component_types == saved.component_types);
    assert(round_trip.resources == saved.resources);
    assert(round_trip.regions.size() == saved.regions.size());
    assert(round_trip.entities.size() == saved.entities.size());
    assert(round_trip.relations.size() == saved.relations.size());
    assert(round_trip.entities[0].persistent_id == saved.entities[0].persistent_id);
    assert(round_trip.entities[1].persistent_id == saved.entities[1].persistent_id);
    assert(round_trip.relations[0].subject == saved.relations[0].subject);
    assert(round_trip.relations[0].kind == saved.relations[0].kind);
    assert(round_trip.relations[0].object == saved.relations[0].object);

    WorldSnapshot invalid = saved;
    invalid.resources.push_back(invalid.resources.front());

    World unchanged;
    const auto sentinel = unchanged.create_entity(fixed_id(88));
    assert(sentinel.has_value());
    assert(!unchanged.restore(invalid));
    assert(unchanged.entity_count() == 1U);
    assert(unchanged.find(fixed_id(88)) == sentinel);
}

} // namespace

int main() {
    test_component_registration();
    test_resource_handles_and_stale_generations();
    test_snapshot_restore_and_transactionality();
    return 0;
}
