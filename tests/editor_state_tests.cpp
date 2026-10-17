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
    const auto mesh_id = vice::world::ResourceId{fixed_id(4)};
    assert(world.create_resource(mesh_id).has_value());
    const auto spatial = world.create_entity(spatial_id);
    const auto plain = world.create_entity(plain_id);
    assert(spatial.has_value());
    assert(plain.has_value());
    assert(world.set_spatial_state(*spatial, vice::world::SpatialState{
        .global_position = vice::world::GlobalPosition{104.0, 2.0, -3.0},
        .region = region_id,
    }));
    const vice::world::AuthoredTransform authored{
        .rotation = {0.0F, 0.0F, 0.70710678F, 0.70710678F},
        .scale = {2.0F, 1.5F, 0.75F},
    };
    assert(world.set_authored_transform(*spatial, authored));
    assert(world.set_resource_reference(*spatial, vice::world::ResourceReference{
        .role = vice::world::ResourceRole::render_mesh,
        .resource = mesh_id,
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
    assert(spatial_inspector->authored_transform == std::optional{authored});
    assert(spatial_inspector->resource_references.size() == 1U);
    assert(spatial_inspector->resource_references.front().role == vice::world::ResourceRole::render_mesh);
    assert(spatial_inspector->resource_references.front().resource == mesh_id);
    assert(spatial_inspector->region.has_value());
    assert(spatial_inspector->region->id == region_id);
    assert(spatial_inspector->simulation_island == vice::world::SimulationIslandId{70});
    assert(spatial_inspector->streaming_cell == (vice::world::StreamingCellId{1, 2, 3}));
    assert(spatial_inspector->relations.size() == 1);

    const auto plain_inspector = vice::editor::project_inspector(world, plain_id);
    assert(plain_inspector.has_value());
    assert(!plain_inspector->spatial_state.has_value());
    assert(!plain_inspector->authored_transform.has_value());
    assert(plain_inspector->resource_references.empty());
    assert(!plain_inspector->region.has_value());
    assert(!plain_inspector->simulation_island.has_value());
    assert(!plain_inspector->streaming_cell.has_value());
    assert(plain_inspector->relations.size() == 1);
    assert(!vice::editor::project_inspector(world, fixed_id(99)).has_value());
}

void test_transform_history() {
    using namespace vice;

    world::World world;
    assert(world.register_region(world::RegionDesc{
        .id = world::RegionId{1},
        .simulation_island = world::SimulationIslandId{1},
        .origin = {},
        .streaming_cell = {},
        .fidelity = world::RegionFidelity::full,
    }));
    const auto persistent_id = fixed_id(50);
    const auto entity = world.create_entity(persistent_id);
    assert(entity.has_value());
    assert(world.set_spatial_state(*entity, world::SpatialState{
        .global_position = {1.0, 2.0, 3.0},
        .region = world::RegionId{1},
    }));

    editor::TransformHistory history;
    history.mark_saved();
    assert(!history.dirty());
    assert(!history.can_undo());
    assert(!history.can_redo());

    const auto before = editor::capture_entity_transform(world, persistent_id);
    assert(before.has_value());

    auto preview = *before;
    preview.spatial_state.global_position.x = 2.0;
    assert(editor::apply_entity_transform(world, persistent_id, preview));
    preview.spatial_state.global_position.x = 3.0;
    assert(editor::apply_entity_transform(world, persistent_id, preview));

    auto moved = preview;
    moved.spatial_state.global_position.x = 5.0;
    moved.authored_transform = world::AuthoredTransform{
        .rotation = {0.0F, 0.0F, 0.0F, 1.0F},
        .scale = {2.0F, 2.0F, 2.0F},
    };
    assert(editor::apply_entity_transform(world, persistent_id, moved));
    assert(history.record(editor::TransformHistory::Entry{
        .persistent_id = persistent_id,
        .before = *before,
        .after = moved,
        .label = "Move Entity",
    }));

    assert(history.size() == 1U);
    assert(history.applied_count() == 1U);
    assert(history.dirty());
    assert(history.can_undo());
    assert(!history.can_redo());
    assert(history.undo_label() == "Move Entity");

    assert(history.undo(world));
    assert(editor::capture_entity_transform(world, persistent_id) == before);
    assert(!history.dirty());
    assert(!history.can_undo());
    assert(history.can_redo());
    assert(history.redo_label() == "Move Entity");

    assert(history.redo(world));
    assert(editor::capture_entity_transform(world, persistent_id) == std::optional{moved});
    history.mark_saved();
    assert(!history.dirty());

    auto scaled = moved;
    scaled.authored_transform->scale = {3.0F, 1.0F, 1.0F};
    assert(editor::apply_entity_transform(world, persistent_id, scaled));
    assert(history.record(editor::TransformHistory::Entry{
        .persistent_id = persistent_id,
        .before = moved,
        .after = scaled,
        .label = "Scale Entity",
    }));
    assert(history.dirty());
    assert(history.undo(world));
    assert(!history.dirty());
    assert(history.can_redo());

    auto rotated = moved;
    rotated.authored_transform->rotation = {0.0F, 0.0F, 0.70710678F, 0.70710678F};
    assert(editor::apply_entity_transform(world, persistent_id, rotated));
    assert(history.record(editor::TransformHistory::Entry{
        .persistent_id = persistent_id,
        .before = moved,
        .after = rotated,
        .label = "Rotate Entity",
    }));
    assert(history.dirty());
    assert(!history.can_redo());
    assert(history.undo(world));
    assert(!history.dirty());
    assert(editor::capture_entity_transform(world, persistent_id) == std::optional{moved});
}

void test_mesh_instance_history() {
    using namespace vice;

    world::World world;
    const world::RegionDesc region{
        .id = world::RegionId{1},
        .simulation_island = world::SimulationIslandId{1},
        .origin = {},
        .streaming_cell = {},
        .fidelity = world::RegionFidelity::full,
        .bounds = world::RegionBounds{
            .min = {-100.0F, -100.0F, -100.0F},
            .max = {100.0F, 100.0F, 100.0F},
        },
    };
    assert(world.register_region(region));
    const world::ResourceId mesh_resource{fixed_id(88)};

    editor::CommandHistory history;
    history.mark_saved();
    const auto persistent_id = editor::create_mesh_instance(
        world,
        history,
        mesh_resource,
        world::GlobalPosition{4.0, 2.0, -6.0}
    );
    assert(persistent_id.has_value());
    assert(world.find(*persistent_id).has_value());
    assert(world.find_resource(mesh_resource).has_value());
    assert(history.size() == 1U);
    assert(history.applied_count() == 1U);
    assert(history.undo_label() == "Add Mesh Instance");

    const auto created = world.find(*persistent_id);
    assert(created.has_value());
    const auto refs = world.resource_references(*created);
    assert(refs.size() == 1U);
    assert(refs.front().role == world::ResourceRole::render_mesh);
    assert(refs.front().resource == mesh_resource);

    const auto initial = editor::capture_entity_transform(world, *persistent_id);
    assert(initial.has_value());
    auto moved = *initial;
    moved.spatial_state.global_position.x = 8.0;
    assert(editor::apply_entity_transform(world, *persistent_id, moved));
    assert(history.record(editor::CommandHistory::TransformEntry{
        .persistent_id = *persistent_id,
        .before = *initial,
        .after = moved,
        .label = "Move Entity",
    }));
    assert(history.size() == 2U);
    assert(history.undo_label() == "Move Entity");

    assert(history.undo(world));
    assert(editor::capture_entity_transform(world, *persistent_id) == initial);
    assert(history.undo_label() == "Add Mesh Instance");
    assert(history.undo(world));
    assert(!world.find(*persistent_id).has_value());
    assert(!world.find_resource(mesh_resource).has_value());
    assert(!history.can_undo());
    assert(history.redo_label() == "Add Mesh Instance");

    assert(history.redo(world));
    const auto recreated = world.find(*persistent_id);
    assert(recreated.has_value());
    assert(world.persistent_id(*recreated) == persistent_id);
    assert(world.resource_references(*recreated).size() == 1U);
    assert(world.resource_references(*recreated).front().resource == mesh_resource);
    assert(editor::capture_entity_transform(world, *persistent_id) == initial);
    assert(history.redo_label() == "Move Entity");

    assert(history.redo(world));
    assert(editor::capture_entity_transform(world, *persistent_id) == std::optional{moved});
    assert(!history.can_redo());
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
    test_transform_history();
    test_mesh_instance_history();
    test_console_history();
    test_editor_state_does_not_mutate_world_snapshot();
    return 0;
}
