#pragma once

#include <vice/world/rigid_box.hpp>
#include <istream>
#include <ostream>
#include <string>

namespace vice::tools::world_cooker {
inline const char* rigid_motion_name(world::RigidMotion motion) {
    switch (motion) {
    case world::RigidMotion::static_body: return "static";
    case world::RigidMotion::kinematic_body: return "kinematic";
    case world::RigidMotion::dynamic_body: return "dynamic";
    }
    return "invalid";
}

inline bool read_rigid_box(std::istream& input, world::RigidBox& box) {
    std::string motion, quality, kind;
    if (!(input >> motion >> box.half_extents.x >> box.half_extents.y >> box.half_extents.z >>
        box.linear_velocity.x >> box.linear_velocity.y >> box.linear_velocity.z >>
        box.angular_velocity.x >> box.angular_velocity.y >> box.angular_velocity.z >>
        box.friction >> box.restitution >> quality >> kind)) return false;
    if (motion == "static") box.motion = world::RigidMotion::static_body;
    else if (motion == "kinematic") box.motion = world::RigidMotion::kinematic_body;
    else if (motion == "dynamic") box.motion = world::RigidMotion::dynamic_body;
    else return false;
    if (quality != "discrete" && quality != "continuous") return false;
    if (kind != "solid" && kind != "trigger") return false;
    box.continuous = quality == "continuous";
    box.trigger = kind == "trigger";
    return world::valid(box);
}

inline void write_rigid_box(std::ostream& output, const world::RigidBox& box) {
    output << rigid_motion_name(box.motion);
    for (const auto v : {box.half_extents, box.linear_velocity, box.angular_velocity}) {
        output << ' ' << v.x << ' ' << v.y << ' ' << v.z;
    }
    output << ' ' << box.friction << ' ' << box.restitution << ' '
        << (box.continuous ? "continuous" : "discrete") << ' ' << (box.trigger ? "trigger" : "solid");
}
} // namespace vice::tools::world_cooker
