#include <vice/jobs/jobs.hpp>
#include <vice/runtime/module_loader.hpp>
#include <vice/runtime/module_api.hpp>
#include <vice/platform/dynamic_library.hpp>
#include <vice/rigid_body/authored_simulation.hpp>
#include <vice/tools/world_cooker.hpp>
#include <vice/world/snapshot_file.hpp>

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace {
void require(bool condition, const char* message) {
    if (!condition) { std::cerr << "Gameplay integration: " << message << '\n'; std::exit(1); }
}
vice::world::PersistentId id(const char* text) { return *vice::world::PersistentId::parse(text); }
}
int main() {
    using namespace vice;
    const auto parsed = tools::world_cooker::parse_source_manifest(
        "vice-world-source 1\nregion 1 10 1000000000000 0 0 0 0 0 full none\nregion 2 20 1000000000000 0 0 0 0 0 inactive none\n"
        "entity 93000000-0000-4000-8000-000000000001 spatial 1000000000000 -0.5 0 1 rigid_box static 100 0.5 100 0 0 0 0 0 0 0.5 0 discrete solid\n"
        "entity 93000000-0000-4000-8000-000000000002 spatial 1000000000000.25 5 0 1 rigid_box dynamic 0.5 0.5 0.5 0 0 0 0 0 0 0.5 0 discrete solid\n"
        "entity 93000000-0000-4000-8000-000000000003 spatial 1000000000020 3 0 1 rigid_box kinematic 0.5 0.5 0.5 0 0 0 0 0 0 0.5 0 discrete solid\n"
        "entity 93000000-0000-4000-8000-000000000004 spatial 1000000000000 3 0 2 rigid_box dynamic 0.5 0.5 0.5 0 0 0 0 0 0 0.5 0 discrete solid\n"
        "entity 93000000-0000-4000-8000-000000000005 none\n");
    require(parsed.snapshot.has_value(), "parse arbitrary authored project");
    world::World world;
    require(world.restore(*parsed.snapshot), "restore project");
    jobs::Scheduler jobs{jobs::SchedulerConfig{.worker_count = 1}};
    rigid_body::AuthoredSimulation physics;
    require(physics.reset(world, jobs), "initialize shared simulation");
    const auto actor_id = id("93000000-0000-4000-8000-000000000002");
    const auto actor = *world.find(actor_id);
    auto loaded = runtime::load_game_module(VICE_RUNTIME_GAMEPLAY_MODULE);
    require(static_cast<bool>(loaded), "load actual compiled gameplay module");
    project::Manifest manifest;
    runtime::GameContext context{manifest, world, &physics};
    runtime::GameModuleLifecycle lifecycle{*loaded.module, context};
    require(lifecycle.start(), "module queries, controls and rejection contracts");
    require(world.rigid_box(actor)->linear_velocity.y == 4 && physics.body(actor_id)->linear_velocity.y == 4,
        "start control immediately publishes actual solver velocity to World");
    std::string diagnostics;
    auto library = platform::DynamicLibrary::load(VICE_RUNTIME_GAMEPLAY_MODULE, &diagnostics);
    require(static_cast<bool>(library), "open exported retained-context probe");
    using Probe = std::uint32_t (*)() noexcept;
    const auto probe = reinterpret_cast<Probe>(library->symbol("vice_gameplay_probe", &diagnostics));
    require(probe && probe() == static_cast<std::uint32_t>(runtime::module::Result::wrong_phase), "retained context rejected outside callback");
    platform::Event key{platform::KeyEvent{.pressed = true}};
    lifecycle.event(key);
    require(world.rigid_box(actor)->linear_velocity.x == 5 && world.rigid_box(actor)->linear_velocity.y == 8,
        "input callback controls arbitrary authored dynamic body");
    lifecycle.update(1.0 / 120.0);
    require(physics.step(&diagnostics), "controlled bodies step without reset or stale-state failure");
    require(world.spatial_state(actor)->global_position.x > 1000000000000.25 && world.spatial_state(actor)->global_position.y > 5,
        "input changes actual simulation at large-world coordinates");
    render::Color color;
    lifecycle.frame(color);
    require(color.red == 0.75F, "render callback can read state but cannot mutate physics");
    world::World resumed;
    const auto encoded = world::encode_world_snapshot(world.snapshot());
    const auto decoded = world::decode_world_snapshot(encoded.bytes);
    require(decoded.snapshot && resumed.restore(*decoded.snapshot), "persist controlled World state");
    rigid_body::AuthoredSimulation restored;
    require(restored.reset(resumed, jobs) && restored.step(), "resume controlled motion");
    require(resumed.spatial_state(*resumed.find(actor_id))->global_position.x > world.spatial_state(actor)->global_position.x,
        "resume retains module-controlled horizontal velocity");
    auto externally_edited = *world.rigid_box(actor);
    externally_edited.friction = 0.75F;
    require(world.set_rigid_box(actor, externally_edited), "external edit fixture");
    const auto before = physics.body(actor_id)->linear_velocity.x;
    require(physics.set_body_velocity(actor_id, {99, 0, 0}, {}) == rigid_body::BodyControlResult::reset_required &&
        physics.body(actor_id)->linear_velocity.x == before, "controls do not conceal stale external body edits");
    lifecycle.stop();
    require(probe() == static_cast<std::uint32_t>(runtime::module::Result::wrong_phase), "retained API rejected after stop");
    return 0;
}
