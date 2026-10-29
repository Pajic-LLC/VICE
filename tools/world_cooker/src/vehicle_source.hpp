#pragma once
#include <vice/world/vehicle.hpp>
#include <istream>
#include <ostream>
namespace vice::tools::world_cooker {
inline bool read_vehicle(std::istream& input,world::Vehicle& v) {
    return static_cast<bool>(input>>v.wheelbase>>v.track>>v.mount_height>>v.wheel_radius>>v.suspension_travel>>v.spring>>v.damping>>v.drive_force
        >>v.brake_force>>v.lateral_stiffness>>v.max_steer>>v.tire_friction>>v.throttle>>v.steering>>v.brake>>v.distance) && world::valid(v);
}
inline void write_vehicle(std::ostream& out,const world::Vehicle& v) {
    out<<v.wheelbase<<' '<<v.track<<' '<<v.mount_height<<' '<<v.wheel_radius<<' '<<v.suspension_travel<<' '<<v.spring<<' '<<v.damping<<' '<<v.drive_force
        <<' '<<v.brake_force<<' '<<v.lateral_stiffness<<' '<<v.max_steer<<' '<<v.tire_friction<<' '<<v.throttle<<' '<<v.steering<<' '<<v.brake<<' '<<v.distance;
}
}
