#pragma once

#include <vice/rigid_body/debug.hpp>
#include <vice/rigid_body/world_bridge.hpp>

#include <memory>
#include <array>
#include <string>

namespace vice::rigid_body {

struct PoweredHingeObservation {
    world::PoweredHinge intent;
    float angle = 0, error = 0, motor_impulse = 0, limit_impulse = 0, linear_impulse = 0;
    bool active = false;
};
struct BodySupport {
    bool supported = false;
    world::PersistentId supporting_entity{};
    world::GlobalPosition point{};
    math::Vec3 normal{};
    math::Vec3 velocity{};
};

struct WheelObservation {
    bool grounded = false;
    world::PersistentId supporting_entity{};
    world::PersistentId material{};
    world::GlobalPosition point{};
    math::Vec3 normal{};
    float suspension_length = 0;
    float normal_force = 0;
    float longitudinal_force = 0;
    float lateral_force = 0;
};
struct VehicleObservation {
    world::Vehicle state;
    float forward_speed = 0;
    std::array<WheelObservation,4> wheels{}; // front left/right, rear left/right
};
enum class AttachmentControlResult { applied, not_found, invalid_argument, inactive_region, reset_required, out_of_range, already_attached, not_attached, blocked };

enum class VehicleControlResult { applied, not_found, invalid_argument, inactive_region, reset_required };

struct CharacterObservation {
    CharacterState state;
    world::PersistentId supporting_entity{};
    CharacterId id{};
};
enum class CharacterControlResult { applied, not_found, invalid_argument, inactive_region, reset_required, not_grounded, attached };

enum class BodyControlResult { applied, not_found, invalid_argument, static_body, inactive_region, reset_required };

// Shared by standalone Runtime and isolated Editor preview. World owns state;
// domains/handles are derived and rebuilt from persistent authoring records.
class AuthoredSimulation {
public:
    AuthoredSimulation();
    ~AuthoredSimulation();
    AuthoredSimulation(const AuthoredSimulation&) = delete;
    AuthoredSimulation& operator=(const AuthoredSimulation&) = delete;

    bool reset(world::World& world, jobs::Scheduler& jobs, std::string* diagnostics = nullptr);
    bool step(std::string* diagnostics = nullptr);
    bool advance(double delta_seconds, std::string* diagnostics = nullptr);
    [[nodiscard]] BodyControlResult set_body_velocity(world::PersistentId owner, math::Vec3 linear, math::Vec3 angular);
    [[nodiscard]] AttachmentControlResult attach_character(world::PersistentId character,world::PersistentId carrier,math::Vec3 offset,float max_distance);
    [[nodiscard]] AttachmentControlResult detach_character(world::PersistentId character,math::Vec3 exit_offset,float max_speed,float max_drop);
    [[nodiscard]] VehicleControlResult control_vehicle(world::PersistentId owner, float throttle, float steering, float brake);
    [[nodiscard]] std::optional<VehicleObservation> vehicle(world::PersistentId owner) const noexcept;
    [[nodiscard]] std::size_t vehicle_count() const noexcept;
    [[nodiscard]] std::optional<PoweredHingeObservation>
    powered_hinge(world::PersistentId owner) const noexcept;
    [[nodiscard]] BodyControlResult control_powered_hinge(world::PersistentId owner,
                                                          const world::JointDrive &drive,
                                                          bool enabled = true);
    [[nodiscard]] std::optional<CharacterObservation> character(world::PersistentId owner) const noexcept;
    [[nodiscard]] CharacterControlResult control_character(world::PersistentId owner, math::Vec3 horizontal, float jump_speed = 0);
    [[nodiscard]] std::size_t character_count() const noexcept;
    [[nodiscard]] const simulation::Coordinator& coordinator() const noexcept;
    [[nodiscard]] std::size_t body_count() const noexcept;
    [[nodiscard]] std::optional<BodyDebugSnapshot> body(world::PersistentId owner) const noexcept;
    [[nodiscard]] const WorldBridge& bridge() const noexcept;
    [[nodiscard]] std::optional<BodySupport> body_support(world::PersistentId owner,
        math::Vec3 up = {0, 1, 0}, float min_up_dot = 0.7F) const noexcept;

private:
    struct State;
    std::unique_ptr<State> state_;
};
} // namespace vice::rigid_body
