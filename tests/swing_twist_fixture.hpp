#pragma once
#include "powered_hinge_fixture.hpp"
inline std::array<float, 4> swing_target(float twist, float y, float z) {
    // Swing (axis in YZ) followed by twist about constraint X.
    const float a = std::hypot(y, z), s = a > 0 ? std::sin(a / 2) / a : 0;
    const float sy = y * s, sz = z * s, c = std::cos(a / 2), tx = std::sin(twist / 2),
                tw = std::cos(twist / 2);
    return {c * tx, sy * tw + sz * tx, sz * tw - sy * tx, c * tw};
}
inline world::WorldSnapshot swing_fixture() {
    auto s = joint_fixture();
    s.entities.back().powered_hinge.reset();
    world::PoweredSwingTwist h;
    h.first = joint_id(1);
    h.second = joint_id(2);
    h.first_frame.axis = h.second_frame.axis = {1, 0, 0};
    h.first_frame.normal = h.second_frame.normal = {0, 1, 0};
    h.second_frame.anchor = {0, .4F, 0};
    h.configuration.drive.target_orientation = swing_target(.35F, .4F, -.3F);
    h.configuration.drive.frequency_hz = 8;
    h.configuration.drive.max_swing_axis_torque = 1000;
    h.configuration.drive.max_twist_torque = 700;
    s.swing_twist_joints.push_back({joint_id(3), h});
    return s;
}
