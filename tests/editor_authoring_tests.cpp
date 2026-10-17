#include <vice/editor/state.hpp>
#include <vice/world/snapshot_file.hpp>

#include <cassert>
#include <filesystem>
#include <string>

namespace {

vice::world::PersistentId fixed_id(const std::uint8_t tail) {
    vice::world::PersistentId::Bytes bytes{};
    bytes[15] = tail;
    return vice::world::PersistentId{bytes};
}

} // namespace

int main() {
    using namespace vice;

    world::World world;
    assert(world.register_region(world::RegionDesc{
        .id = world::RegionId{1},
        .simulation_island = world::SimulationIslandId{101},
        .origin = {},
        .streaming_cell = {},
        .fidelity = world::RegionFidelity::full,
        .bounds = world::RegionBounds{
            .min = {-100.0F, -100.0F, -100.0F},
            .max = {100.0F, 100.0F, 100.0F},
        },
    }));

    const auto persistent_id = fixed_id(77);
    const auto entity = world.create_entity(persistent_id);
    assert(entity.has_value());
    assert(world.set_spatial_state(*entity, world::SpatialState{
        .global_position = {2.0, 1.0, -3.0},
        .region = world::RegionId{1},
    }));

    editor::TransformHistory history;
    history.mark_saved();
    const auto before = editor::capture_entity_transform(world, persistent_id);
    assert(before.has_value());

    auto after = *before;
    after.spatial_state.global_position = {7.5, 3.0, -1.25};
    after.authored_transform = world::AuthoredTransform{
        .rotation = {0.0F, 0.38268343F, 0.0F, 0.92387953F},
        .scale = {1.25F, 2.0F, 0.75F},
    };
    assert(editor::apply_entity_transform(world, persistent_id, after));
    assert(history.record(editor::TransformHistory::Entry{
        .persistent_id = persistent_id,
        .before = *before,
        .after = after,
        .label = "Author Entity",
    }));
    assert(history.dirty());

    const auto path = std::filesystem::temp_directory_path() / "vice_editor_authoring_test.vworldc";
    std::error_code error;
    std::filesystem::remove(path, error);
    std::string diagnostics;
    assert(world::save_world_snapshot(path, world.snapshot(), &diagnostics));
    assert(diagnostics.empty());
    history.mark_saved();
    assert(!history.dirty());

    const auto loaded = world::load_world_snapshot(path);
    assert(loaded.snapshot.has_value());

    world::World restored;
    assert(restored.restore(*loaded.snapshot));
    const auto restored_state = editor::capture_entity_transform(restored, persistent_id);
    assert(restored_state.has_value());
    assert(*restored_state == after);

    std::filesystem::remove(path, error);
    return 0;
}
