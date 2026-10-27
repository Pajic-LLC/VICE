#pragma once

#include <vice/rigid_body/debug.hpp>
#include <vice/rigid_body/world_bridge.hpp>

#include <memory>
#include <string>

namespace vice::rigid_body {

struct BodySupport {
    bool supported = false;
    world::PersistentId supporting_entity{};
    world::GlobalPosition point{};
    math::Vec3 normal{};
    math::Vec3 velocity{};
};

struct CharacterObservation {
    CharacterState state;
    world::PersistentId supporting_entity{};
    CharacterId id{};
};
enum class CharacterControlResult { applied, not_found, invalid_argument, inactive_region, reset_required, not_grounded };

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
