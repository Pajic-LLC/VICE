#include <vice/world/world.hpp>
#include <atomic>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
namespace vice::world {
bool World::advance_vehicle_revision() noexcept {
    static std::atomic<std::uint64_t> next{1};
    auto value=next.load(std::memory_order_relaxed);
    for(;;) {
        if(value==std::numeric_limits<std::uint64_t>::max()) return false;
        if(next.compare_exchange_weak(value,value+1,std::memory_order_relaxed)) {vehicle_revision_=value;return true;}
    }
}
bool validate_vehicles(const WorldSnapshot& snapshot,std::string* diagnostics) {
    for(const auto& e:snapshot.entities) if(e.vehicle && (!valid(*e.vehicle) || !e.spatial_state ||
        !e.rigid_box || e.rigid_box->motion!=RigidMotion::dynamic_body || e.rigid_box->trigger || e.character)) {
        if(diagnostics)*diagnostics="Vehicle "+e.persistent_id.to_string()+": requires valid settings and a dynamic solid chassis.";
        return false;
    }
    return true;
}
bool World::set_vehicle(EntityId e,const Vehicle& v) {
    if(!alive(e) || !valid(v))return false;
    auto& s=slots_[e.index];
    if(!s.spatial_state || !s.rigid_box || s.rigid_box->motion!=RigidMotion::dynamic_body || s.rigid_box->trigger || s.character)return false;
    if(s.vehicle==v)return true;
    if(!advance_vehicle_revision())return false;
    s.vehicle=v;return true;
}
bool World::clear_vehicle(EntityId e) {
    if(!alive(e))return false;
    auto& v=slots_[e.index].vehicle;
    if(v) {if(!advance_vehicle_revision())return false;v.reset();}
    return true;
}
std::optional<Vehicle> World::vehicle(EntityId e) const noexcept {return alive(e)?slots_[e.index].vehicle:std::nullopt;}
std::string serialize_vehicle(const std::optional<Vehicle>& value) {
    if(!value)return "null";
    const auto& v=*value;
    std::ostringstream out;out.imbue(std::locale::classic());out<<std::setprecision(std::numeric_limits<double>::max_digits10);
    out<<"{\"wheelbase\":"<<v.wheelbase<<",\"track\":"<<v.track<<",\"mount_height\":"<<v.mount_height
       <<",\"wheel_radius\":"<<v.wheel_radius<<",\"suspension_travel\":"<<v.suspension_travel<<",\"spring\":"<<v.spring
       <<",\"damping\":"<<v.damping<<",\"drive_force\":"<<v.drive_force<<",\"brake_force\":"<<v.brake_force
       <<",\"lateral_stiffness\":"<<v.lateral_stiffness<<",\"max_steer\":"<<v.max_steer<<",\"tire_friction\":"<<v.tire_friction
       <<",\"throttle\":"<<v.throttle<<",\"steering\":"<<v.steering<<",\"brake\":"<<v.brake<<",\"distance\":"<<v.distance<<'}';
    return out.str();
}
}
