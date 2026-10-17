#include <vice/editor/state.hpp>

#include <cassert>
#include <source_location>
#include <vector>

namespace {

vice::world::PersistentId fixed_id(const std::uint8_t tail) {
    vice::world::PersistentId::Bytes bytes{};
    bytes[15] = tail;
    return vice::world::PersistentId{bytes};
}

void test_selection_survives_restore() {
    vice::world::World source;
    const auto selected_id = fixed_id(20);
    const auto other_id = fixed_id(10);

    const auto selected_entity = source.create_entity(selected_id);
    const auto other_entity = source.create_entity(other_id);
    assert(selected_entity.has_value());
    assert(other_entity.has_value());

    vice::editor::SelectionModel selection;
    selection.select(selected_id);
    assert(selection.resolve(source) == selected_entity);

    vice::world::World restored;
    assert(restored.restore(source.snapshot()));
    const auto restored_selected = selection.resolve(restored);
    assert(restored_selected.has_value());
    assert(restored.persistent_id(*restored_selected) == selected_id);
}

void test_hierarchy_is_deterministic() {
    vice::world::World world;
    assert(world.create_entity(fixed_id(30)).has_value());
    assert(world.create_entity(fixed_id(10)).has_value());
    assert(world.create_entity(fixed_id(20)).has_value());

    const auto hierarchy = vice::editor::build_hierarchy(world);
    assert(hierarchy.size() == 3);
    assert(hierarchy[0].persistent_id == fixed_id(10));
    assert(hierarchy[1].persistent_id == fixed_id(20));
    assert(hierarchy[2].persistent_id == fixed_id(30));
}

void test_inspector_projection() {
    vice::world::World world;
    const vice::world::RegionId region_id{7};
    assert(world.register_region(vice::world::RegionDesc{
        .id = region_id,
        .simulation_island = vice::world::SimulationIslandId{70},
        .origin = vice::world::GlobalPosition{100.0, 0.0, 0.0},
        .streaming_cell = vice::world::StreamingCellId{1, 2, 3},
        .fidelity = vice::world::RegionFidelity::full,
    }));

    const auto spatial_id = fixed_id(1);
    const auto plain_id = fixed_id(2);
    const auto kind_id = fixed_id(3);
    const auto spatial = world.create_entity(spatial_id);
    const auto plain = world.create_entity(plain_id);
    assert(spatial.has_value());
    assert(plain.has_value());
    assert(world.set_spatial_state(*spatial, vice::world::SpatialState{
        .global_position = vice::world::GlobalPosition{104.0, 2.0, -3.0},
        .region = region_id,
    }));
    assert(world.add_relation(vice::world::Relation{
        .subject = *spatial,
        .kind = kind_id,
        .object = *plain,
    }));

    const auto spatial_inspector = vice::editor::project_inspector(world, spatial_id);
    assert(spatial_inspector.has_value());
    assert(spatial_inspector->entity == spatial);
    assert(spatial_inspector->spatial_state.has_value());
    assert(spatial_inspector->region.has_value());
    assert(spatial_inspector->region->id == region_id);
    assert(spatial_inspector->simulation_island == vice::world::SimulationIslandId{70});
    assert(spatial_inspector->streaming_cell == (vice::world::StreamingCellId{1, 2, 3}));
    assert(spatial_inspector->relations.size() == 1);

    const auto plain_inspector = vice::editor::project_inspector(world, plain_id);
    assert(plain_inspector.has_value());
    assert(!plain_inspector->spatial_state.has_value());
    assert(!plain_inspector->region.has_value());
    assert(!plain_inspector->simulation_island.has_value());
    assert(!plain_inspector->streaming_cell.has_value());
    assert(plain_inspector->relations.size() == 1);
    assert(!vice::editor::project_inspector(world, fixed_id(99)).has_value());
}

void test_console_history() {
    using namespace vice::core;

    vice::editor::ConsoleHistory history{2};
    history.push(LogRecord{LogLevel::info, "one", std::source_location::current()});
    history.push(LogRecord{LogLevel::warning, "two", std::source_location::current()});
    history.push(LogRecord{LogLevel::error, "three", std::source_location::current()});

    assert(history.capacity() == 2);
    assert(history.size() == 2);
    const auto all = history.entries();
    assert(all.size() == 2);
    assert(all[0].message == "two");
    assert(all[1].message == "three");
    assert(all[0].sequence < all[1].sequence);

    const auto errors = history.entries(LogLevel::error);
    assert(errors.size() == 1);
    assert(errors.front().message == "three");

    std::vector<LogRecord> primary;
    set_min_log_level(LogLevel::debug);
    set_log_sink([&primary](const LogRecord& record) {
        primary.push_back(record);
    });
    history.clear();
    assert(history.attach());
    assert(history.attached());
    log(LogLevel::info, "observed");
    assert(primary.size() == 1);
    assert(history.size() == 1);
    assert(history.entries().front().message == "observed");
    history.detach();
    assert(!history.attached());
    log(LogLevel::warning, "primary only");
    assert(primary.size() == 2);
    assert(history.size() == 1);
    reset_log_sink();
}

void test_editor_state_does_not_mutate_world_snapshot() {
    vice::world::World world;
    const auto id = fixed_id(42);
    assert(world.create_entity(id).has_value());
    const auto before = world.snapshot();

    vice::editor::SelectionModel selection;
    selection.select(id);
    vice::editor::ConsoleHistory console{4};
    console.push(vice::core::LogRecord{
        vice::core::LogLevel::info,
        "editor-only state",
        std::source_location::current(),
    });

    const auto after = world.snapshot();
    assert(before.entities.size() == after.entities.size());
    assert(before.relations.size() == after.relations.size());
    assert(before.resources.size() == after.resources.size());
    assert(before.regions.size() == after.regions.size());
    assert(before.component_types.size() == after.component_types.size());
    assert(before.entities.front().persistent_id == after.entities.front().persistent_id);
}

} // namespace

int main() {
    test_selection_survives_restore();
    test_hierarchy_is_deterministic();
    test_inspector_projection();
    test_console_history();
    test_editor_state_does_not_mutate_world_snapshot();
    return 0;
}
