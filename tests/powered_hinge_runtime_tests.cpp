#include "powered_hinge_fixture.hpp"
#include <vice/jobs/jobs.hpp>
#include <vice/rigid_body/authored_simulation.hpp>
#include <vice/runtime/debug.hpp>
#include <vice/runtime/module_loader.hpp>
#if defined(VICE_POWERED_EDITOR)
#include <vice/editor/preview_runtime.hpp>
#endif
void collision_filter_lifetime(jobs::Scheduler &jobs) {
    auto d = rigid_body::Domain::create(simulation::DomainId{927}, "articulation.filter", jobs);
    require(static_cast<bool>(d), "filter domain");
    rigid_body::BoxBodyDesc a;
    a.participant = {1};
    a.motion = rigid_body::MotionType::static_body;
    a.position = {0, 2, 0};
    auto b = a;
    b.participant = {2};
    b.motion = rigid_body::MotionType::dynamic_body;
    require(d->create_box(a) && d->create_box(b), "overlapping linked bodies");
    rigid_body::HingeConstraintDesc h;
    h.id = {1};
    h.first = {1};
    h.second = {2};
    h.anchor = {0, 2, 0};
    h.collide_connected = false;
    require(d->create_constraint(h), "first collision exclusion");
    h.id = {2};
    require(d->create_constraint(h), "second collision exclusion");
    simulation::Coordinator c{1.0 / 120};
    require(c.register_domain(*d), "collision domain binding");
    c.step();
    require(d->contact_events().empty(), "connected overlap does not fight itself");
    require(d->destroy_constraint({1}), "remove first reference");
    c.step();
    require(d->contact_events().empty(), "remaining joint keeps collision excluded");
    require(d->destroy_constraint({2}), "remove last reference");
    c.step();
    require(!d->contact_events().empty(), "normal collisions resume after last joint removed");
}
int main() {
    jobs::Scheduler jobs{jobs::SchedulerConfig{.worker_count = 1}};
    collision_filter_lifetime(jobs);
    world::World w;
    require(w.restore(joint_fixture()), "fixture");
    rigid_body::AuthoredSimulation sim;
    std::string diagnostics;
    require(sim.reset(w, jobs, &diagnostics), diagnostics.c_str());
    float peak = 0;
    for (unsigned i = 0; i < 480; ++i) {
        require(sim.step(&diagnostics), diagnostics.c_str());
        const auto o = sim.powered_hinge(joint_id(3));
        require(o.has_value(), "joint observation");
        peak = std::max(peak, o->motor_impulse);
        require(o->motor_impulse <= 1000.0F / 120 + .001F,
                "torque cap is respected by actual motor impulse");
    }
    auto held = *sim.powered_hinge(joint_id(3));
    std::cout << "held angle " << held.angle << " error " << held.error << " peak impulse " << peak
              << '\n';
    require(std::abs(held.error) < .06F && peak > 0, "gravity-loaded actuator holds pose");
    require(sim.set_body_velocity(joint_id(2), {0, 0, 0}, {0, 0, -8}) ==
                rigid_body::BodyControlResult::applied,
            "angular disturbance");
    for (unsigned i = 0; i < 360; ++i)
        require(sim.step(), "recover pose");
    auto recovered = *sim.powered_hinge(joint_id(3));
    require(std::abs(recovered.error) < .06F, "pose recovery under bounded motor torque");
    const auto bytes = world::encode_world_snapshot(w.snapshot()).bytes;
    world::World loaded;
    require(loaded.restore(*world::decode_world_snapshot(bytes).snapshot), "restore checkpoint");
    rigid_body::AuthoredSimulation resumed;
    require(resumed.reset(loaded, jobs), "rebuild loaded actuator");
    const auto first = *resumed.powered_hinge(joint_id(3));
    require(std::abs(first.angle - recovered.angle) < .002F,
            "local frames preserve nonzero angular reference after load");
    for (unsigned i = 0; i < 120; ++i)
        require(resumed.step(), "resume pose");
    require(std::abs(resumed.powered_hinge(joint_id(3))->error) < .06F,
            "saved pose target remains coherent");
    auto drive = resumed.powered_hinge(joint_id(3))->intent.drive;
    drive.target_angle = -.5F;
    require(resumed.control_powered_hinge(joint_id(3), drive) ==
                rigid_body::BodyControlResult::applied,
            "live drive command");
    for (unsigned i = 0; i < 240; ++i)
        require(resumed.step(), "retarget");
    require(std::abs(resumed.powered_hinge(joint_id(3))->error) < .06F, "retarget holds new angle");
    drive.mode = world::JointDriveMode::off;
    require(resumed.control_powered_hinge(joint_id(3), drive) ==
                rigid_body::BodyControlResult::applied,
            "motor off");
    for (unsigned i = 0; i < 120; ++i)
        require(resumed.step(), "passive limb");
    require(resumed.powered_hinge(joint_id(3))->motor_impulse == 0,
            "disabled motor applies no actuation");
    auto region = *loaded.region(world::RegionId{1});
    region.fidelity = world::RegionFidelity::inactive;
    require(loaded.set_region_fidelity(region.id, region.fidelity), "inactive fidelity");
    const auto before = loaded.spatial_state(*loaded.find(joint_id(2)));
    for (unsigned i = 0; i < 20; ++i)
        require(resumed.step(), "inactive step");
    require(loaded.spatial_state(*loaded.find(joint_id(2))) == before &&
                !resumed.powered_hinge(joint_id(3))->active,
            "inactive state retained without active drive");
    auto changed = *loaded.powered_hinge(*loaded.find(joint_id(3)));
    changed.first_frame.anchor.x = .1F;
    require(loaded.set_powered_hinge(*loaded.find(joint_id(3)), changed), "structural edit");
    const auto step = resumed.coordinator().step_index();
    require(!resumed.step() && resumed.coordinator().step_index() == step,
            "structural edit fails before advancing simulation");
    world::World chain;
    require(chain.restore(joint_fixture()), "chain fixture");
    const auto limb = *chain.create_entity(joint_id(4)), joint = *chain.create_entity(joint_id(5));
    require(chain.set_spatial_state(limb, {{1e12, 3.8, 0}, world::RegionId{1}}) &&
                chain.set_rigid_box(limb, *chain.rigid_box(*chain.find(joint_id(2)))),
            "second articulated link");
    auto second = *chain.powered_hinge(*chain.find(joint_id(3)));
    second.first = joint_id(2);
    second.second = joint_id(4);
    second.first_frame.anchor = {0, -.4F, 0};
    second.drive.target_angle = -.4F;
    require(chain.set_powered_hinge(joint, second), "second powered joint");
    rigid_body::AuthoredSimulation chain_sim;
    require(chain_sim.reset(chain, jobs), "multi-joint reconstruction");
    for (unsigned i = 0; i < 720; ++i)
        require(chain_sim.step(), "articulated chain pose");
    const auto a1 = *chain_sim.powered_hinge(joint_id(3)),
               a2 = *chain_sim.powered_hinge(joint_id(5));
    std::cout << "chain errors " << a1.error << " / " << a2.error << '\n';
    require(std::abs(a1.error) < .08F && std::abs(a2.error) < .08F,
            "coupled joints hold distinct targets under gravity");
    world::World chain_load;
    require(chain_load.restore(chain.snapshot()), "chain checkpoint");
    rigid_body::AuthoredSimulation chain_resumed;
    require(chain_resumed.reset(chain_load, jobs), "restore coupled articulations");
    require(std::abs(chain_resumed.powered_hinge(joint_id(3))->angle - a1.angle) < .002F &&
                std::abs(chain_resumed.powered_hinge(joint_id(5))->angle - a2.angle) < .002F,
            "all joint references survive checkpoint");
    world::World weak;
    require(weak.restore(joint_fixture()), "weak fixture");
    auto weak_h = *weak.powered_hinge(*weak.find(joint_id(3)));
    weak_h.drive.max_torque = 1;
    require(weak.set_powered_hinge(*weak.find(joint_id(3)), weak_h), "weak actuator intent");
    rigid_body::AuthoredSimulation weak_sim;
    require(weak_sim.reset(weak, jobs), "weak actuator");
    float weak_peak = 0;
    for (unsigned i = 0; i < 240; ++i) {
        require(weak_sim.step(), "weak pose");
        const auto o = *weak_sim.powered_hinge(joint_id(3));
        weak_peak = std::max(weak_peak, o.motor_impulse);
        require(o.motor_impulse <= 1.0F / 120 + .0001F, "weak torque cap enforced");
    }
    require(std::abs(weak_sim.powered_hinge(joint_id(3))->error) > .4F && weak_peak > .007F,
            "weak actuator cannot hold gravity load");
    auto module = runtime::load_game_module(VICE_POWERED_TEST_MODULE);
    require(static_cast<bool>(module), "load actual module");
    project::Manifest manifest;
    runtime::GameModuleLifecycle lifecycle{*module.module, {manifest, w, &sim}};
    require(lifecycle.start(), "size/thread guards and SDK drive update");
    require(sim.step(), "module target applied");
    render::Color color;
    lifecycle.frame(color);
    require(color.red == 1, "frame can inspect but not control joints");
    lifecycle.stop();
    const auto debug = runtime::serialize_runtime_debug_entity_snapshot(
        runtime::sample_runtime_debug_entity(w, joint_id(3), &sim), "test");
    require(debug.find("powered_hinge_state") != std::string::npos &&
                debug.find("motor_impulse") != std::string::npos,
            "structured decisions inspectable");
#if defined(VICE_POWERED_EDITOR)
    world::World source;
    require(source.restore(joint_fixture()), "Editor source");
    editor::AssetIndex assets;
    editor::PreviewRuntime preview;
    const editor::PreviewGameConfig config{project::Manifest{}, {}};
    require(preview.reset(source, jobs, assets, &diagnostics, &config), "Editor clone");
    for (unsigned i = 0; i < 180; ++i)
        require(preview.step_once(), "Editor physical pose");
    require(source.authored_transform(*source.find(joint_id(2))) !=
                preview.world()->authored_transform(*preview.world()->find(joint_id(2))),
            "Play pose isolated from source");
    require(preview.reset(source, jobs, assets, &diagnostics, &config), "Editor reset");
    require(preview.world()->powered_hinge(*preview.world()->find(joint_id(3))) ==
                source.powered_hinge(*source.find(joint_id(3))),
            "Play reset preserves authored intent");
#endif
}
