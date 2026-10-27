#include <vice/jobs/jobs.hpp>
#if defined(VICE_SUPPORT_EDITOR_PREVIEW)
#include <vice/editor/preview_runtime.hpp>
#endif
#include <vice/rigid_body/authored_simulation.hpp>
#include <vice/runtime/module_loader.hpp>
#include <vice/tools/world_cooker.hpp>
#include <vice/world/snapshot_file.hpp>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
namespace {
void require(bool ok, const char* message) { if (!ok) { std::cerr << "Body support: " << message << '\n'; std::exit(1); } }
vice::world::PersistentId id(const char* text) { return *vice::world::PersistentId::parse(text); }
}
int main() {
    using namespace vice;
    const auto actor_id = id("95000000-0000-4000-8000-000000000002");
    const auto floor_id = id("95000000-0000-4000-8000-000000000001");
    const auto parsed = tools::world_cooker::parse_source_manifest(
        "vice-world-source 1\nregion 1 10 1000000000000 0 0 0 0 0 full none\n"
        "entity 95000000-0000-4000-8000-000000000001 spatial 1000000000000 49.5 0 1 rigid_box static 10 0.5 10 0 0 0 0 0 0 0.5 0 discrete solid\n"
        "entity 95000000-0000-4000-8000-000000000002 spatial 1000000000000.25 55 0 1 rigid_box dynamic 0.5 0.5 0.5 0 0 0 0 0 0 0.5 0 continuous solid\n");
    require(parsed.snapshot.has_value(), "parse elevated arbitrary authored floor");
    jobs::Scheduler jobs{jobs::SchedulerConfig{.worker_count = 1}};
    world::World world; require(world.restore(*parsed.snapshot), "restore world");
    rigid_body::AuthoredSimulation physics; require(physics.reset(world, jobs), "create simulation");
    require(physics.body_support(actor_id) && !physics.body_support(actor_id)->supported, "no fabricated support before first contact");
    for (int tick = 0; tick < 1200; ++tick) require(physics.step(), "fall and settle");
    require(!physics.body(actor_id)->active, "body sleeps on static floor");
    const auto support = physics.body_support(actor_id);
    require(support && support->supported && support->supporting_entity == floor_id && support->normal.y > 0.99F &&
        std::abs(support->point.y - 50) < 0.05 && std::abs(support->point.x - 1000000000000.25) < 1,
        "sleeping contact retains real elevated support and large-world point");
    require(physics.body_support(floor_id, {0, -1, 0})->supported, "support direction is caller-selected and normals are oriented toward the querying body");
    require(!physics.body_support(actor_id, {1, 0, 0})->supported, "floor is not support for a horizontal up direction");
    require(!physics.body_support(actor_id, {}) && !physics.body_support(actor_id, {0, 1, 0}, 2) &&
        !physics.body_support(actor_id, {std::numeric_limits<float>::quiet_NaN(), 0, 0}), "invalid support policy rejected");
    world::World resumed;
    const auto encoded = world::encode_world_snapshot(world.snapshot());
    require(resumed.restore(*world::decode_world_snapshot(encoded.bytes).snapshot), "save settled world");
    rigid_body::AuthoredSimulation resumed_physics;
    require(resumed_physics.reset(resumed, jobs) && resumed_physics.step() &&
        resumed_physics.body_support(actor_id)->supported, "support is derived afresh after save/resume without cached solver contacts");
#if defined(VICE_SUPPORT_EDITOR_PREVIEW)
    const auto resumed_height = resumed.spatial_state(*resumed.find(actor_id))->global_position.y;
    editor::AssetIndex assets;
    editor::PreviewRuntime preview;
    const editor::PreviewGameConfig config{project::Manifest{}, VICE_SUPPORT_TEST_MODULE};
    std::string preview_diagnostics;
    require(preview.reset(resumed, jobs, assets, &preview_diagnostics, &config), "grounded module starts in Editor preview before a physics tick");
    preview.play(); preview.set_input_focus(true);
    require(preview.handle_event(platform::Event{platform::KeyEvent{.key = platform::Key::space, .pressed = true}}) &&
        preview.update(1.0 / 120.0) && preview.clear_color().green == 1 &&
        preview.world()->rigid_box(*preview.world()->find(actor_id))->linear_velocity.y > 5,
        "same contact-grounded module jumps in isolated Editor preview");
    require(resumed.spatial_state(*resumed.find(actor_id))->global_position.y == resumed_height,
        "preview support and jump do not mutate authored World");
    preview.stop();
#endif
    auto module = runtime::load_game_module(VICE_SUPPORT_TEST_MODULE);
    require(static_cast<bool>(module), "load SDK-only grounded gameplay module");
    project::Manifest manifest;
    runtime::GameModuleLifecycle lifecycle{*module.module, {manifest, world, &physics}};
    require(lifecycle.start(), "module sees sleeping support and validates query boundary");
    const platform::Event jump{platform::KeyEvent{.key = platform::Key::space, .pressed = true}};
    lifecycle.event(jump); lifecycle.update(1.0 / 120.0);
    require(world.rigid_box(*world.find(actor_id))->linear_velocity.y == 6, "gameplay jumps from contact at y=50 without a height heuristic");
    lifecycle.event(jump); lifecycle.update(1.0 / 120.0);
    render::Color color; lifecycle.frame(color);
    require(color.red == 0.75F && color.green == 1, "a cached contact cannot trigger a repeated upward jump");
    for (int tick = 0; tick < 30; ++tick) require(physics.step(), "jump simulation");
    require(!physics.body_support(actor_id)->supported, "separation clears support");
    lifecycle.event(jump); lifecycle.update(1.0 / 120.0); lifecycle.frame(color);
    require(color.green == 1, "airborne input cannot jump");
    lifecycle.stop();

    world::World trigger;
    require(trigger.restore(*parsed.snapshot), "trigger fixture");
    auto sensor = *trigger.rigid_box(*trigger.find(floor_id)); sensor.trigger = true;
    require(trigger.set_rigid_box(*trigger.find(floor_id), sensor), "sensor floor");
    require(trigger.set_spatial_state(*trigger.find(actor_id), {{1000000000000.25, 50.5, 0}, world::RegionId{1}}), "sensor overlap pose");
    rigid_body::AuthoredSimulation sensing; require(sensing.reset(trigger, jobs), "sensor simulation");
    for (int tick = 0; tick < 30; ++tick) require(sensing.step(), "pass through sensor");
    require(!sensing.body_support(actor_id)->supported && trigger.spatial_state(*trigger.find(actor_id))->global_position.y < 50.4,
        "trigger overlap never counts as physical support");

    world::World moving; require(moving.restore(*parsed.snapshot), "moving floor fixture");
    auto platform = *moving.rigid_box(*moving.find(floor_id));
    platform.motion = world::RigidMotion::kinematic_body; platform.linear_velocity = {1.5F, 0, 0}; platform.angular_velocity = {0, 0.5F, 0};
    require(moving.set_rigid_box(*moving.find(floor_id), platform) &&
        moving.set_spatial_state(*moving.find(actor_id), {{1000000000000.25, 50.5, 0}, world::RegionId{1}}), "moving/rotating platform pose");
    rigid_body::AuthoredSimulation carrying; require(carrying.reset(moving, jobs), "platform simulation");
    for (int tick = 0; tick < 120; ++tick) require(carrying.step(), "platform movement");
    const auto carried = carrying.body_support(actor_id);
    const auto center = moving.spatial_state(*moving.find(floor_id))->global_position;
    require(carried && carried->supported && carried->supporting_entity == floor_id &&
        std::abs(carried->velocity.x - (1.5 + 0.5 * (carried->point.z - center.z))) < 0.01 &&
        std::abs(carried->velocity.z + 0.5 * (carried->point.x - center.x)) < 0.01,
        "support velocity includes linear and angular surface motion");
    require(moving.set_region_fidelity(world::RegionId{1}, world::RegionFidelity::inactive) &&
        !carrying.body_support(actor_id)->supported, "inactive region does not expose stale support");
    return 0;
}
