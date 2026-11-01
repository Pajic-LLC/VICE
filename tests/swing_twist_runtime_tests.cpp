#include "swing_twist_fixture.hpp"
#include <vice/jobs/jobs.hpp>
#include <vice/rigid_body/authored_simulation.hpp>
#include <vice/runtime/debug.hpp>
#include <vice/runtime/module_loader.hpp>
#if defined(VICE_SWING_EDITOR)
#include <vice/editor/preview_runtime.hpp>
#endif
int main() {
    jobs::Scheduler jobs{jobs::SchedulerConfig{.worker_count = 1}};
    world::World w;
    require(w.restore(swing_fixture()), "restore");
    rigid_body::AuthoredSimulation sim;
    std::string diagnostics;
    require(sim.reset(w, jobs, &diagnostics), diagnostics.c_str());
    float peak = 0;
    for (unsigned i = 0; i < 480; ++i) {
        require(sim.step(&diagnostics), diagnostics.c_str());
        auto o = sim.swing_twist_joint(joint_id(3));
        require(o.has_value(), "native observation");
        peak = std::max(peak, std::abs(o->motor_impulses.y));
        require(std::abs(o->motor_impulses.x) <= 700.F / 120 + .001F &&
                    std::abs(o->motor_impulses.y) <= 1000.F / 120 + .001F &&
                    std::abs(o->motor_impulses.z) <= 1000.F / 120 + .001F,
                "per-axis torque caps");
    }
    auto held = *sim.swing_twist_joint(joint_id(3));
    std::cout << "held error " << held.angular_error << " peak " << peak << '\n';
    require(held.angular_error < .08F && peak > 0, "gravity-loaded 3-axis pose");
    require(sim.set_body_velocity(joint_id(2), {0, 0, 0}, {4, -5, 3}) ==
                rigid_body::BodyControlResult::applied,
            "3-axis disturbance");
    for (unsigned i = 0; i < 360; ++i)
        require(sim.step(), "recovery");
    require(sim.swing_twist_joint(joint_id(3))->angular_error < .08F, "disturbed pose recovery");
    const auto before = *sim.swing_twist_joint(joint_id(3));
    const auto bytes = world::encode_world_snapshot(w.snapshot()).bytes;
    world::World loaded;
    require(loaded.restore(*world::decode_world_snapshot(bytes).snapshot), "checkpoint load");
    rigid_body::AuthoredSimulation resumed;
    require(resumed.reset(loaded, jobs), "cold reset");
    const auto after = *resumed.swing_twist_joint(joint_id(3));
    const auto a = before.orientation, b = after.orientation;
    const auto dot = std::abs(a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w);
    require(dot > .99999F, "nonzero 3-axis reference survives checkpoint");
    auto d = after.intent.configuration.drive;
    d.target_orientation = swing_target(-.45F, -.3F, .35F);
    require(resumed.control_swing_twist_joint(joint_id(3), d) ==
                rigid_body::BodyControlResult::applied,
            "retarget");
    for (unsigned i = 0; i < 300; ++i)
        require(resumed.step(), "new pose");
    require(resumed.swing_twist_joint(joint_id(3))->angular_error < .08F, "retarget holds");
    auto invalid = d;
    invalid.target_orientation = swing_target(2, 0, 0);
    const auto unchanged = world::encode_world_snapshot(loaded.snapshot()).bytes;
    require(resumed.control_swing_twist_joint(joint_id(3), invalid) ==
                    rigid_body::BodyControlResult::invalid_argument &&
                world::encode_world_snapshot(loaded.snapshot()).bytes == unchanged,
            "out-of-limit control atomic");
    d.swing_mode = d.twist_mode = world::JointDriveMode::off;
    require(resumed.control_swing_twist_joint(joint_id(3), d) ==
                rigid_body::BodyControlResult::applied,
            "passive mode");
    for (unsigned i = 0; i < 120; ++i)
        require(resumed.step(), "passive step");
    auto passive = *resumed.swing_twist_joint(joint_id(3));
    require(math::dot(passive.motor_impulses, passive.motor_impulses) == 0,
            "no actuation when off");
    require(loaded.set_region_fidelity({1}, world::RegionFidelity::inactive), "inactive");
    const auto position = loaded.spatial_state(*loaded.find(joint_id(2)));
    for (unsigned i = 0; i < 20; ++i)
        require(resumed.step(), "inactive step");
    require(loaded.spatial_state(*loaded.find(joint_id(2))) == position &&
                !resumed.swing_twist_joint(joint_id(3))->active,
            "inactive retains pose");
    auto h = *loaded.swing_twist_joint(*loaded.find(joint_id(3)));
    h.configuration.swing_half_angle = .9F;
    require(loaded.set_swing_twist_joint(*loaded.find(joint_id(3)), h), "structural edit");
    auto step = resumed.coordinator().step_index();
    require(!resumed.step() && resumed.coordinator().step_index() == step &&
                !resumed.swing_twist_joint(joint_id(3)),
            "stale binding rejected before advance");
    // Different physical frame directions must preserve the same constraint-space meaning.
    for (unsigned axis = 0; axis < 3; ++axis) {
        auto fixture = swing_fixture();
        auto &joint = fixture.swing_twist_joints[0].joint;
        if (axis == 1) {
            joint.first_frame.axis = joint.second_frame.axis = {0, 1, 0};
            joint.first_frame.normal = joint.second_frame.normal = {0, 0, 1};
        }
        if (axis == 2) {
            joint.first_frame.axis = joint.second_frame.axis = {0, 0, 1};
            joint.first_frame.normal = joint.second_frame.normal = {1, 0, 0};
        }
        world::World oriented;
        require(oriented.restore(fixture), "oriented fixture");
        rigid_body::AuthoredSimulation driven;
        require(driven.reset(oriented, jobs), "oriented native frames");
        for (unsigned i = 0; i < 480; ++i)
            require(driven.step(), "oriented pose");
        const auto observed = *driven.swing_twist_joint(joint_id(3));
        require(observed.angular_error < .08F, "orientation-independent target");
        const auto &frame = joint.first_frame;
        const auto z = math::cross(frame.axis, frame.normal);
        auto basis = math::identity();
        const std::array columns{frame.axis, frame.normal, z};
        for (unsigned c = 0; c < 3; ++c) {
            basis(0, c) = columns[c].x;
            basis(1, c) = columns[c].y;
            basis(2, c) = columns[c].z;
        }
        auto inverse = math::identity();
        for (unsigned row = 0; row < 3; ++row)
            for (unsigned column = 0; column < 3; ++column)
                inverse(row, column) = basis(column, row);
        const auto &q = joint.configuration.drive.target_orientation;
        const auto expected = math::multiply(
            math::multiply(basis, math::rotation({q[0], q[1], q[2], q[3]})), inverse);
        const auto rotation = oriented.authored_transform(*oriented.find(joint_id(2)))->rotation;
        const auto actual = math::rotation({rotation.x, rotation.y, rotation.z, rotation.w});
        float axis_error = 0;
        for (unsigned row = 0; row < 3; ++row)
            for (unsigned column = 0; column < 3; ++column)
                axis_error =
                    std::max(axis_error, std::abs(actual(row, column) - expected(row, column)));
        require(axis_error < .08F, "actual World rotation follows VICE twist X / normal Y basis");

        world::World restored;
        require(restored.restore(oriented.snapshot()), "oriented restore");
        rigid_body::AuthoredSimulation cold;
        require(cold.reset(restored, jobs), "oriented reconstruction");
        require(std::abs(cold.swing_twist_joint(joint_id(3))->angular_error -
                         observed.angular_error) < .001F,
                "oriented cold reference");
    }
    // Mixed one-axis and multi-axis chains share the existing body and collision truth.
    world::World chain;
    require(chain.restore(swing_fixture()), "mixed fixture");
    const auto limb = *chain.create_entity(joint_id(4)), hinge = *chain.create_entity(joint_id(5));
    require(chain.set_spatial_state(limb, {{1e12, 3.8, 0}, {1}}) &&
                chain.set_rigid_box(limb, *chain.rigid_box(*chain.find(joint_id(2)))),
            "mixed second link");
    auto link = *joint_fixture().entities.back().powered_hinge;
    link.first = joint_id(2);
    link.second = joint_id(4);
    link.first_frame.anchor = {0, -.4F, 0};
    link.drive.target_angle = -.3F;
    require(chain.set_powered_hinge(hinge, link), "mixed hinge");
    rigid_body::AuthoredSimulation mixed;
    require(mixed.reset(chain, jobs), "mixed native assembly");
    for (unsigned i = 0; i < 720; ++i)
        require(mixed.step(), "mixed step");
    require(mixed.swing_twist_joint(joint_id(3))->angular_error < .09F &&
                std::abs(mixed.powered_hinge(joint_id(5))->error) < .09F,
            "mixed chain holds distinct targets");
    auto edited = *chain.rigid_box(*chain.find(joint_id(2)));
    edited.angular_velocity.x = 1;
    require(chain.set_rigid_box(*chain.find(joint_id(2)), edited), "external endpoint edit");
    require(!mixed.swing_twist_joint(joint_id(3)) &&
                mixed.control_swing_twist_joint(joint_id(3), before.intent.configuration.drive) ==
                    rigid_body::BodyControlResult::reset_required,
            "stale endpoint control rejected");
    auto module = runtime::load_game_module(VICE_SWING_TEST_MODULE);
    require(static_cast<bool>(module), "module load");
    project::Manifest manifest;
    runtime::GameModuleLifecycle lifecycle{*module.module, {manifest, w, &sim}};
    require(lifecycle.start(), "module read/control guards");
    require(sim.step(), "module target apply");
    render::Color color;
    lifecycle.frame(color);
    require(color.red == 1, "module frame read guard");
    lifecycle.stop();
    const auto debug = runtime::serialize_runtime_debug_entity_snapshot(
        runtime::sample_runtime_debug_entity(w, joint_id(3), &sim), "test");
    require(debug.find("swing_twist_state") != std::string::npos &&
                debug.find("angular_error") != std::string::npos &&
                debug.find("motor_impulses") != std::string::npos,
            "inspect state");
#if defined(VICE_SWING_EDITOR)
    world::World source;
    require(source.restore(swing_fixture()), "editor source");
    editor::AssetIndex assets;
    editor::PreviewRuntime preview;
    const editor::PreviewGameConfig config{project::Manifest{}, {}};
    require(preview.reset(source, jobs, assets, &diagnostics, &config), "editor clone");
    for (unsigned i = 0; i < 180; ++i)
        require(preview.step_once(), "editor step");
    require(source.authored_transform(*source.find(joint_id(2))) !=
                preview.world()->authored_transform(*preview.world()->find(joint_id(2))),
            "play isolated");
    require(preview.reset(source, jobs, assets, &diagnostics, &config) &&
                preview.world()->swing_twist_joint(*preview.world()->find(joint_id(3))) ==
                    source.swing_twist_joint(*source.find(joint_id(3))),
            "editor reset");
#endif
}
