#pragma once
#include <cmath>
#include <optional>
#include <string>
namespace vice::world {
// Four ray-suspended wheels, Y-up, +Z forward. Physical meters, independent of
// visual scale. The owning dynamic, solid RigidBox supplies mass and inertia.
struct Vehicle {
    float wheelbase = 2.5F;
    float track = 1.5F;
    float mount_height = 0;
    float wheel_radius = 0.32F;
    float suspension_travel = 0.5F;
    float spring = 30000;
    float damping = 3500;
    float drive_force = 6000;
    float brake_force = 12000;
    float lateral_stiffness = 4000;
    float max_steer = 0.5F;
    float tire_friction = 1;
    float throttle = 0; // [-1,1], reverse is negative
    float steering = 0; // [-1,1], positive turns toward +X
    float brake = 0;    // [0,1]
    double distance = 0; // integrated chassis travel in meters
    friend bool operator==(const Vehicle&, const Vehicle&) = default;
};
[[nodiscard]] inline bool valid(const Vehicle& v) noexcept {
    const auto bounded=[](float x,float lo,float hi){return std::isfinite(x)&&x>=lo&&x<=hi;};
    return bounded(v.wheelbase,0.1F,100) && bounded(v.track,0.1F,100) && bounded(v.mount_height,-100,100) &&
        bounded(v.wheel_radius,0.01F,10) && bounded(v.suspension_travel,0.01F,10) &&
        bounded(v.spring,1,1e7F) && bounded(v.damping,0,1e6F) && bounded(v.drive_force,0,1e7F) &&
        bounded(v.brake_force,0,1e7F) && bounded(v.lateral_stiffness,0,1e6F) &&
        bounded(v.max_steer,0,1.2F) && bounded(v.tire_friction,0,10) &&
        bounded(v.throttle,-1,1) && bounded(v.steering,-1,1) && bounded(v.brake,0,1) &&
        std::isfinite(v.distance) && v.distance>=0;
}
[[nodiscard]] std::string serialize_vehicle(const std::optional<Vehicle>& vehicle);
}
