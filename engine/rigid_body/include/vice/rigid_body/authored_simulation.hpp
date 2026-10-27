#pragma once

#include <vice/rigid_body/debug.hpp>
#include <vice/rigid_body/world_bridge.hpp>

#include <memory>
#include <string>

namespace vice::rigid_body {

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
    [[nodiscard]] const simulation::Coordinator& coordinator() const noexcept;
    [[nodiscard]] std::size_t body_count() const noexcept;
    [[nodiscard]] std::optional<BodyDebugSnapshot> body(world::PersistentId owner) const noexcept;
    [[nodiscard]] const WorldBridge& bridge() const noexcept;

private:
    struct State;
    std::unique_ptr<State> state_;
};
} // namespace vice::rigid_body
