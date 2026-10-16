#include <vice/simulation/entity_bindings.hpp>
#include <vice/world/world.hpp>

#include <cassert>
#include <string>
#include <utility>

namespace {

class MockDomain final : public vice::simulation::Domain {
public:
    MockDomain(
        const vice::simulation::DomainId id,
        std::string name
    ) : id_{id}, name_{std::move(name)} {}

    [[nodiscard]] vice::simulation::DomainId id() const noexcept override {
        return id_;
    }

    [[nodiscard]] std::string_view name() const noexcept override {
        return name_;
    }

    [[nodiscard]] vice::simulation::Fidelity fidelity() const noexcept override {
        return fidelity_;
    }

    void set_fidelity(const vice::simulation::Fidelity fidelity) noexcept override {
        fidelity_ = fidelity;
    }

    void step(
        const vice::simulation::StepContext&,
        vice::simulation::InteractionEmitter&
    ) override {}

private:
    vice::simulation::DomainId id_{};
    std::string name_;
    vice::simulation::Fidelity fidelity_ = vice::simulation::Fidelity::full;
};

} // namespace

int main() {
    using namespace vice;

    world::World world;
    simulation::Coordinator coordinator;
    simulation::EntityBindings bindings;

    MockDomain rigid{simulation::DomainId{1}, "rigid"};
    MockDomain thermal{simulation::DomainId{2}, "thermal"};
    assert(coordinator.register_domain(rigid));
    assert(coordinator.register_domain(thermal));
    assert(coordinator.registered(rigid.id()));
    assert(coordinator.registered(thermal.id()));

    const auto entity = world.create_entity();
    const auto other = world.create_entity();
    assert(world.alive(entity));
    assert(world.alive(other));

    assert(bindings.bind(
        world,
        coordinator,
        entity,
        rigid.id(),
        simulation::ParticipantId{100}
    ));
    assert(bindings.bind(
        world,
        coordinator,
        entity,
        thermal.id(),
        simulation::ParticipantId{100}
    ));
    assert(bindings.bind(
        world,
        coordinator,
        other,
        rigid.id(),
        simulation::ParticipantId{101}
    ));

    assert(!bindings.bind(
        world,
        coordinator,
        entity,
        rigid.id(),
        simulation::ParticipantId{102}
    ));
    assert(!bindings.bind(
        world,
        coordinator,
        other,
        rigid.id(),
        simulation::ParticipantId{100}
    ));
    assert(!bindings.bind(
        world,
        coordinator,
        other,
        thermal.id(),
        simulation::ParticipantId{}
    ));
    assert(!bindings.bind(
        world,
        coordinator,
        other,
        simulation::DomainId{99},
        simulation::ParticipantId{200}
    ));

    const auto rigid_participant = bindings.find(world, coordinator, entity, rigid.id());
    const auto thermal_participant = bindings.find(world, coordinator, entity, thermal.id());
    assert(rigid_participant.has_value());
    assert(thermal_participant.has_value());
    assert(*rigid_participant == simulation::ParticipantId{100});
    assert(*thermal_participant == simulation::ParticipantId{100});
    assert(bindings.bindings().size() == 3U);

    assert(coordinator.unregister_domain(thermal.id()));
    assert(!coordinator.registered(thermal.id()));
    assert(!bindings.find(world, coordinator, entity, thermal.id()).has_value());
    assert(bindings.prune(world, coordinator) == 1U);
    assert(bindings.bindings().size() == 2U);

    assert(world.destroy_entity(entity));
    assert(!world.alive(entity));
    assert(!bindings.find(world, coordinator, entity, rigid.id()).has_value());

    const auto replacement = world.create_entity();
    assert(replacement.index == entity.index);
    assert(replacement.generation != entity.generation);
    assert(world.alive(replacement));
    assert(!bindings.find(world, coordinator, replacement, rigid.id()).has_value());

    assert(bindings.prune(world, coordinator) == 1U);
    assert(bindings.bindings().size() == 1U);
    assert(bindings.bind(
        world,
        coordinator,
        replacement,
        rigid.id(),
        simulation::ParticipantId{102}
    ));
    assert(bindings.find(world, coordinator, replacement, rigid.id()) ==
        simulation::ParticipantId{102});

    assert(bindings.unbind(other, rigid.id()));
    assert(!bindings.unbind(other, rigid.id()));
    assert(bindings.remove_entity(replacement) == 1U);
    assert(bindings.bindings().empty());

    assert(bindings.bind(
        world,
        coordinator,
        replacement,
        rigid.id(),
        simulation::ParticipantId{300}
    ));
    assert(bindings.remove_domain(rigid.id()) == 1U);
    assert(bindings.bindings().empty());

    return 0;
}
