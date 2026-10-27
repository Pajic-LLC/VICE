#pragma once
#include <vice/world/character.hpp>
#include <istream>
#include <ostream>
namespace vice::tools::world_cooker {
inline bool read_character(std::istream& input, world::Character& c) {
    float jump = 0;
    if (!(input >> c.height >> c.radius >> c.mass >> c.max_strength >> c.max_slope_angle >> c.step_height >> c.stick_to_floor_distance >>
        c.velocity.x >> c.velocity.y >> c.velocity.z >> c.horizontal_velocity.x >> c.horizontal_velocity.y >> c.horizontal_velocity.z >> jump)) return false;
    if (!std::isfinite(jump) || jump < 0) return false;
    if (jump > 0) c.pending_jump_speed = jump;
    return world::valid(c);
}
inline void write_character(std::ostream& output, const world::Character& c) {
    output << c.height << ' ' << c.radius << ' ' << c.mass << ' ' << c.max_strength << ' ' << c.max_slope_angle << ' ' << c.step_height << ' ' << c.stick_to_floor_distance << ' '
        << c.velocity.x << ' ' << c.velocity.y << ' ' << c.velocity.z << ' ' << c.horizontal_velocity.x << ' ' << c.horizontal_velocity.y << ' ' << c.horizontal_velocity.z << ' ' << c.pending_jump_speed.value_or(0);
}
}
