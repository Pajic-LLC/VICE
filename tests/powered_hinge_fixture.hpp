#pragma once
#include <cstdlib>
#include <iostream>
#include <limits>
#include <vice/tools/world_authoring.hpp>
#include <vice/tools/world_cooker.hpp>
#include <vice/tools/world_edit.hpp>
#include <vice/world/snapshot_file.hpp>
#include <vice/world/world.hpp>
using namespace vice;
inline void require(bool ok, const char *what) {
    if (!ok) {
        std::cerr << "Powered hinge: " << what << '\n';
        std::exit(1);
    }
}
inline world::PersistentId joint_id(unsigned n) {
    auto b = world::PersistentId::Bytes{};
    b[0] = 0xd7;
    b[6] = 0x40;
    b[8] = 0x80;
    b[15] = static_cast<std::uint8_t>(n);
    return world::PersistentId{b};
}
inline world::WorldSnapshot joint_fixture() {
    world::World w;
    require(w.register_region({world::RegionId{1},
                               world::SimulationIslandId{10},
                               {1e12, 0, 0},
                               {},
                               world::RegionFidelity::full,
                               {}}),
            "region");
    auto a = *w.create_entity(joint_id(1)), b = *w.create_entity(joint_id(2)),
         j = *w.create_entity(joint_id(3));
    require(w.set_spatial_state(a, {{1e12, 5, 0}, world::RegionId{1}}) &&
                w.set_spatial_state(b, {{1e12, 4.6, 0}, world::RegionId{1}}),
            "positions");
    world::RigidBox base;
    base.motion = world::RigidMotion::static_body;
    base.half_extents = {.1F, .1F, .1F};
    world::RigidBox limb;
    limb.half_extents = {.1F, .4F, .1F};
    require(w.set_rigid_box(a, base) && w.set_rigid_box(b, limb), "bodies");
    world::PoweredHinge h;
    h.first = joint_id(1);
    h.second = joint_id(2);
    h.second_frame.anchor = {0, .4F, 0};
    h.min_angle = -1.2F;
    h.max_angle = 1.2F;
    h.drive.target_angle = .7F;
    h.drive.frequency_hz = 8;
    h.drive.max_torque = 1000;
    require(w.set_powered_hinge(j, h), "joint");
    return w.snapshot();
}
