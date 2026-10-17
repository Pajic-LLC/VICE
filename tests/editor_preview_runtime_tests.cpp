#include <vice/editor/asset_index.hpp>
#include <vice/editor/preview_runtime.hpp>
#include <vice/jobs/jobs.hpp>
#include <vice/world/persistent_id.hpp>
#include <vice/world/world.hpp>

#include <cstdlib>
#include <iostream>
#include <string_view>

namespace {

constexpr std::string_view player_id_text = "10000000-0000-4000-8000-000000000001";
constexpr std::string_view cube_id_text = "10000000-0000-4000-8000-000000000002";

void require(const bool condition, const std::string_view message) {
    if (condition) return;
    std::cerr << "editor preview test failed: " << message << '\n';
    std::exit(1);
}

vice::world::World make_world() {
    using namespace vice;
    world::World world;

    world::RegionDesc first;
    first.id = world::RegionId{1};
    first.simulation_island = world::SimulationIslandId{101};
    first.origin = {-8.0, 0.0, 0.0};
    first.streaming_cell = {0, 0, 0};
    first.fidelity = world::RegionFidelity::full;
    first.bounds = world::RegionBounds{
        .min = {0.0F, -8.0F, -8.0F},
        .max = {8.0F, 8.0F, 8.0F},
    };
    require(world.register_region(first), "first region");

    world::RegionDesc second = first;
    second.id = world::RegionId{2};
    second.simulation_island = world::SimulationIslandId{202};
    second.origin = {0.0, 0.0, 0.0};
    second.streaming_cell = {1, 0, 0};
    second.fidelity = world::RegionFidelity::inactive;
    require(world.register_region(second), "second region");

    const auto player_id = world::PersistentId::parse(player_id_text);
    const auto cube_id = world::PersistentId::parse(cube_id_text);
    require(player_id.has_value() && cube_id.has_value(), "role ids");

    const auto player = world.create_entity(*player_id);
    const auto cube = world.create_entity(*cube_id);
    require(player.has_value() && cube.has_value(), "role entities");
    require(world.set_spatial_state(*player, world::SpatialState{
        .global_position = {-4.0, 0.0, 0.0},
        .region = world::RegionId{1},
    }), "player spatial");
    require(world.set_spatial_state(*cube, world::SpatialState{
        .global_position = {-2.0, 2.0, 0.0},
        .region = world::RegionId{1},
    }), "cube spatial");
    return world;
}

} // namespace

int main() {
    using namespace vice;

    auto authored = make_world();
    const auto cube_id = world::PersistentId::parse(cube_id_text);
    require(cube_id.has_value(), "cube id parse");
    const auto authored_cube = authored.find(*cube_id);
    require(authored_cube.has_value(), "authored cube");
    const auto original_spatial = authored.spatial_state(*authored_cube);
    require(original_spatial.has_value(), "original cube spatial");

    jobs::Scheduler jobs{jobs::SchedulerConfig{.worker_count = 1}};
    editor::AssetIndex empty_assets;
    editor::PreviewRuntime preview;
    std::string diagnostics;
    require(preview.reset(authored, jobs, empty_assets, &diagnostics), "preview reset");
    require(preview.active(), "preview active");
    require(preview.playback() == editor::PreviewPlayback::paused, "preview starts paused");
    require(preview.coordinator() != nullptr, "preview coordinator");
    require(preview.coordinator()->step_index() == 0U, "initial step index");

    require(preview.update(1.0), "paused update");
    require(preview.coordinator()->step_index() == 0U, "paused update does not step");

    require(preview.step_once(), "single step");
    require(preview.coordinator()->step_index() == 1U, "single step increments exactly once");

    const auto after_step_authored = authored.spatial_state(*authored_cube);
    require(after_step_authored.has_value(), "authored cube still exists");
    require(*after_step_authored == *original_spatial, "preview cannot mutate authored World");

    auto snapshot = preview.snapshot();
    require(snapshot.playback == editor::PreviewPlayback::paused, "snapshot paused state");
    require(snapshot.step_index == 1U, "snapshot step index");
    require(snapshot.regions.size() == 2U, "snapshot regions");
    require(snapshot.bodies.size() == 1U, "snapshot cube body");
    require(snapshot.characters.size() == 1U, "snapshot character");
    require(snapshot.bodies.front().owner == *cube_id, "body stable owner");

    preview.play();
    require(preview.playback() == editor::PreviewPlayback::running, "play state");
    require(preview.update(1.0 / 60.0), "running update");
    require(preview.coordinator()->step_index() >= 2U, "running preview advances");
    preview.pause();
    const auto paused_step = preview.coordinator()->step_index();
    require(preview.update(1.0 / 60.0), "second paused update");
    require(preview.coordinator()->step_index() == paused_step, "pause freezes preview");

    require(preview.reset(authored, jobs, empty_assets, &diagnostics), "preview reset from authored");
    require(preview.coordinator()->step_index() == 0U, "reset clears simulation time");
    const auto* preview_world = preview.world();
    require(preview_world != nullptr, "reset preview world");
    const auto preview_cube = preview_world->find(*cube_id);
    require(preview_cube.has_value(), "reset cube identity");
    const auto reset_spatial = preview_world->spatial_state(*preview_cube);
    require(reset_spatial.has_value() && *reset_spatial == *original_spatial, "reset restores authored transform");

    preview.stop();
    require(!preview.active(), "stop releases preview");
    require(preview.coordinator() == nullptr, "stopped preview has no coordinator");
    return 0;
}
