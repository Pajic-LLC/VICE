#pragma once
#include <cmath>
#include <istream>
#include <ostream>
#include <vice/math/math.hpp>
#include <vice/world/persistent_id.hpp>
namespace vice::world {
enum class JointDriveMode : unsigned { off, velocity, position, position_and_velocity };
// Body-local COM coordinates in metres; axes/normals form an orthonormal reference frame.
struct JointFrame {
    math::Vec3 anchor{}, axis{0, 0, 1}, normal{1, 0, 0};
};
inline bool operator==(const JointFrame &a, const JointFrame &b) {
    return a.anchor.x == b.anchor.x && a.anchor.y == b.anchor.y && a.anchor.z == b.anchor.z &&
           a.axis.x == b.axis.x && a.axis.y == b.axis.y && a.axis.z == b.axis.z &&
           a.normal.x == b.normal.x && a.normal.y == b.normal.y && a.normal.z == b.normal.z;
}
struct JointDrive {
    JointDriveMode mode = JointDriveMode::position;
    float target_angle = 0, target_velocity = 0, frequency_hz = 4, damping_ratio = 1,
          max_torque = 100;
    friend bool operator==(const JointDrive &, const JointDrive &) = default;
};
struct PoweredHinge {
    PersistentId first{}, second{};
    JointFrame first_frame{}, second_frame{};
    float min_angle = -3.14159265F, max_angle = 3.14159265F;
    JointDrive drive{};
    bool enabled = true;
    friend bool operator==(const PoweredHinge &, const PoweredHinge &) = default;
};
inline bool valid(const JointFrame &f) noexcept {
    const auto finite = [](math::Vec3 v) {
        return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
    };
    return finite(f.anchor) && finite(f.axis) && finite(f.normal) &&
           std::abs(math::dot(f.axis, f.axis) - 1) < 1e-4F &&
           std::abs(math::dot(f.normal, f.normal) - 1) < 1e-4F &&
           std::abs(math::dot(f.axis, f.normal)) < 1e-4F;
}
inline bool valid(const JointDrive &d) noexcept {
    return static_cast<unsigned>(d.mode) <= 3 && std::isfinite(d.target_angle) &&
           std::isfinite(d.target_velocity) && std::isfinite(d.frequency_hz) &&
           d.frequency_hz >= 0 && d.frequency_hz <= 120 && std::isfinite(d.damping_ratio) &&
           d.damping_ratio >= 0 && d.damping_ratio <= 10 && std::isfinite(d.max_torque) &&
           d.max_torque >= 0 && (d.mode == JointDriveMode::off || d.max_torque > 0) &&
           ((d.mode != JointDriveMode::position &&
             d.mode != JointDriveMode::position_and_velocity) ||
            d.frequency_hz > 0);
}
inline bool valid(const PoweredHinge &h) noexcept {
    return !h.first.is_nil() && !h.second.is_nil() && h.first != h.second && valid(h.first_frame) &&
           valid(h.second_frame) && std::isfinite(h.min_angle) && std::isfinite(h.max_angle) &&
           h.min_angle >= -3.14159265F && h.min_angle <= 0 && h.max_angle >= 0 &&
           h.max_angle <= 3.14159265F && h.min_angle <= h.max_angle && valid(h.drive) &&
           h.drive.target_angle >= h.min_angle && h.drive.target_angle <= h.max_angle;
}
inline void write_powered_hinge(std::ostream &o, const PoweredHinge &h) {
    o << 1 << ' ' << h.first.to_string() << ' ' << h.second.to_string();
    for (const auto &f : {h.first_frame, h.second_frame})
        for (auto v : {f.anchor, f.axis, f.normal})
            o << ' ' << v.x << ' ' << v.y << ' ' << v.z;
    o << ' ' << h.min_angle << ' ' << h.max_angle << ' ' << static_cast<unsigned>(h.drive.mode)
      << ' ' << h.drive.target_angle << ' ' << h.drive.target_velocity << ' '
      << h.drive.frequency_hz << ' ' << h.drive.damping_ratio << ' ' << h.drive.max_torque << ' '
      << h.enabled;
}
inline bool read_powered_hinge(std::istream &in, PoweredHinge &out) {
    PoweredHinge h;
    unsigned version = 0, mode = 0, enabled = 0;
    std::string a, b;
    if (!(in >> version >> a >> b) || version != 1)
        return false;
    const auto first = PersistentId::parse(a), second = PersistentId::parse(b);
    if (!first || !second)
        return false;
    h.first = *first;
    h.second = *second;
    for (auto *f : {&h.first_frame, &h.second_frame})
        for (auto *v : {&f->anchor, &f->axis, &f->normal})
            if (!(in >> v->x >> v->y >> v->z))
                return false;
    if (!(in >> h.min_angle >> h.max_angle >> mode >> h.drive.target_angle >>
          h.drive.target_velocity >> h.drive.frequency_hz >> h.drive.damping_ratio >>
          h.drive.max_torque >> enabled) ||
        mode > 3 || enabled > 1)
        return false;
    h.drive.mode = static_cast<JointDriveMode>(mode);
    h.enabled = enabled != 0;
    if (!valid(h))
        return false;
    out = h;
    return true;
}
} // namespace vice::world
