#pragma once
#include <algorithm>
#include <array>
#include <vice/world/powered_hinge.hpp>
namespace vice::world {
struct SwingTwistDrive {
    JointDriveMode swing_mode = JointDriveMode::position, twist_mode = JointDriveMode::position;
    std::array<float, 4> target_orientation{0, 0, 0, 1}; // constraint-space quaternion, twist=X
    std::array<float, 3> target_velocity{};
    float frequency_hz = 4, damping_ratio = 1, max_swing_axis_torque = 100, max_twist_torque = 100;
    friend bool operator==(const SwingTwistDrive &, const SwingTwistDrive &) = default;
};
struct SwingTwist {
    float swing_half_angle = 1, twist_min = -1, twist_max = 1;
    SwingTwistDrive drive{};
    friend bool operator==(const SwingTwist &, const SwingTwist &) = default;
};
inline bool valid(const SwingTwistDrive &d) noexcept {
    double norm = 0;
    for (auto v : d.target_orientation) {
        if (!std::isfinite(v))
            return false;
        norm += double(v) * v;
    }
    for (auto v : d.target_velocity)
        if (!std::isfinite(v))
            return false;
    const auto mode = [](JointDriveMode m) { return static_cast<unsigned>(m) <= 3; };
    const auto position = [](JointDriveMode m) {
        return m == JointDriveMode::position || m == JointDriveMode::position_and_velocity;
    };
    return mode(d.swing_mode) && mode(d.twist_mode) && std::abs(norm - 1) < 1e-4 &&
           std::isfinite(d.frequency_hz) && d.frequency_hz >= 0 && d.frequency_hz <= 120 &&
           std::isfinite(d.damping_ratio) && d.damping_ratio >= 0 && d.damping_ratio <= 10 &&
           std::isfinite(d.max_swing_axis_torque) && d.max_swing_axis_torque >= 0 &&
           std::isfinite(d.max_twist_torque) && d.max_twist_torque >= 0 &&
           (d.swing_mode == JointDriveMode::off || d.max_swing_axis_torque > 0) &&
           (d.twist_mode == JointDriveMode::off || d.max_twist_torque > 0) &&
           (!(position(d.swing_mode) || position(d.twist_mode)) || d.frequency_hz > 0);
}
inline bool valid(const SwingTwist &s) noexcept {
    if (!valid(s.drive) || !std::isfinite(s.swing_half_angle) || s.swing_half_angle < 0 ||
        s.swing_half_angle > 3.13F || !std::isfinite(s.twist_min) || !std::isfinite(s.twist_max) ||
        s.twist_min < -3.14159265F || s.twist_min > 0 || s.twist_max < 0 ||
        s.twist_max > 3.14159265F)
        return false;
    const auto &q = s.drive.target_orientation;
    const double n =
        double(q[0]) * q[0] + double(q[1]) * q[1] + double(q[2]) * q[2] + double(q[3]) * q[3];
    const double t = std::hypot(double(q[0]), double(q[3]));
    if (t < 1e-8)
        return false;
    const double swing = 2 * std::acos(std::clamp(t / std::sqrt(n), 0.0, 1.0));
    double twist = 2 * std::atan2(double(q[0]), double(q[3]));
    twist = std::remainder(twist, 2 * 3.141592653589793);
    return swing <= s.swing_half_angle + 1e-6 && twist >= s.twist_min - 1e-6 &&
           twist <= s.twist_max + 1e-6;
}

struct PoweredSwingTwist {
    PersistentId first{}, second{};
    JointFrame first_frame{}, second_frame{};
    SwingTwist configuration{};
    bool enabled = true;
    float break_threshold = 0, last_break_impulse = 0;
    std::uint64_t break_step = 0;
    bool broken = false;
    friend bool operator==(const PoweredSwingTwist &, const PoweredSwingTwist &) = default;
};
inline bool valid(const PoweredSwingTwist &j) noexcept {
    return !j.first.is_nil() && !j.second.is_nil() && j.first != j.second && valid(j.first_frame) &&
           valid(j.second_frame) && valid(j.configuration) &&
           std::isfinite(j.break_threshold) && j.break_threshold >= 0 &&
           std::isfinite(j.last_break_impulse) && j.last_break_impulse >= 0 &&
           (j.broken ? (!j.enabled && j.break_threshold > 0 && j.last_break_impulse > j.break_threshold && j.break_step > 0)
                     : (j.last_break_impulse == 0 && j.break_step == 0));
}
struct OwnedSwingTwist {
    PersistentId owner{};
    PoweredSwingTwist joint;
};
inline void write_swing_twist(std::ostream &o, const PoweredSwingTwist &h) {
    o << 2 << ' ' << h.first.to_string() << ' ' << h.second.to_string();
    for (const auto &f : {h.first_frame, h.second_frame})
        for (auto v : {f.anchor, f.axis, f.normal})
            o << ' ' << v.x << ' ' << v.y << ' ' << v.z;
    const auto &s = h.configuration;
    const auto &d = s.drive;
    o << ' ' << s.swing_half_angle << ' ' << s.twist_min << ' ' << s.twist_max << ' '
      << static_cast<unsigned>(d.swing_mode) << ' ' << static_cast<unsigned>(d.twist_mode);
    for (auto v : d.target_orientation)
        o << ' ' << v;
    for (auto v : d.target_velocity)
        o << ' ' << v;
    o << ' ' << d.frequency_hz << ' ' << d.damping_ratio << ' ' << d.max_swing_axis_torque << ' '
      << d.max_twist_torque << ' ' << static_cast<unsigned>(h.enabled) << ' ' << h.break_threshold << ' ' << h.last_break_impulse << ' ' << h.break_step << ' ' << static_cast<unsigned>(h.broken);
}
inline bool read_swing_twist(std::istream &in, PoweredSwingTwist &out) {
    PoweredSwingTwist h;
    unsigned version = 0, sm = 0, tm = 0, enabled = 0;
    std::string a, b;
    if (!(in >> version >> a >> b) || (version != 1 && version != 2))
        return false;
    auto first = PersistentId::parse(a), second = PersistentId::parse(b);
    if (!first || !second)
        return false;
    h.first = *first;
    h.second = *second;
    for (auto *f : {&h.first_frame, &h.second_frame})
        for (auto *v : {&f->anchor, &f->axis, &f->normal})
            if (!(in >> v->x >> v->y >> v->z))
                return false;
    auto &s = h.configuration;
    auto &d = s.drive;
    if (!(in >> s.swing_half_angle >> s.twist_min >> s.twist_max >> sm >> tm) || sm > 3 || tm > 3)
        return false;
    for (auto &v : d.target_orientation)
        if (!(in >> v))
            return false;
    for (auto &v : d.target_velocity)
        if (!(in >> v))
            return false;
    if (!(in >> d.frequency_hz >> d.damping_ratio >> d.max_swing_axis_torque >>
          d.max_twist_torque >> enabled) ||
        enabled > 1)
        return false;
    d.swing_mode = static_cast<JointDriveMode>(sm);
    d.twist_mode = static_cast<JointDriveMode>(tm);
    h.enabled = enabled != 0;
    if(version == 2) {
        unsigned broken=0;
        if(!(in >> h.break_threshold >> h.last_break_impulse >> h.break_step >> broken) || broken>1) return false;
        h.broken=broken!=0;
    }
    if (!valid(h))
        return false;
    out = h;
    return true;
}
} // namespace vice::world
