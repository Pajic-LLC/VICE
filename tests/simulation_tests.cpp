#include <vice/simulation/simulation.hpp>

#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <variant>
#include <vector>

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

    void emit_next_to(const vice::simulation::DomainId target) noexcept {
        emit_target_ = target;
        emit_next_ = true;
    }

    void step(
        const vice::simulation::StepContext& context,
        vice::simulation::InteractionEmitter& emitter
    ) override {
        ++steps_;
        last_step_index_ = context.step_index;
        last_delta_seconds_ = context.fixed_delta_seconds;
        received_.insert(received_.end(), context.incoming.begin(), context.incoming.end());

        if (emit_next_) {
            emit_next_ = false;
            std::vector<std::byte> payload{std::byte{0x2A}};
            assert(emitter.emit(emit_target_, "mock.contact", std::move(payload)));
        }
    }

    [[nodiscard]] std::uint64_t steps() const noexcept { return steps_; }
    [[nodiscard]] std::uint64_t last_step_index() const noexcept { return last_step_index_; }
    [[nodiscard]] double last_delta_seconds() const noexcept { return last_delta_seconds_; }
    [[nodiscard]] const std::vector<vice::simulation::InteractionEvent>& received() const noexcept {
        return received_;
    }

private:
    vice::simulation::DomainId id_{};
    std::string name_;
    vice::simulation::Fidelity fidelity_ = vice::simulation::Fidelity::full;
    vice::simulation::DomainId emit_target_{};
    bool emit_next_ = false;
    std::uint64_t steps_ = 0;
    std::uint64_t last_step_index_ = 0;
    double last_delta_seconds_ = 0.0;
    std::vector<vice::simulation::InteractionEvent> received_;
};

} // namespace

int main() {
    using namespace vice::simulation;

    Material material;
    assert(material.set_property("physics", "density_kg_m3", 998.2));
    assert(material.set_property("acoustics", "absorption", 0.17));
    assert(material.set_property("render", "surface_class", std::string{"water"}));
    assert(!material.set_property("", "invalid", 1.0));

    const auto* density = material.find_property("physics", "density_kg_m3");
    assert(density != nullptr);
    assert(std::abs(std::get<double>(density->value) - 998.2) < 0.0001);
    assert(material.properties().size() == 3U);
    assert(material.set_property("physics", "density_kg_m3", 1000.0));
    assert(material.properties().size() == 3U);
    density = material.find_property("physics", "density_kg_m3");
    assert(density != nullptr && std::get<double>(density->value) == 1000.0);
    assert(material.remove_property("render", "surface_class"));
    assert(material.find_property("render", "surface_class") == nullptr);

    Coordinator coordinator{1.0 / 120.0};
    assert(std::abs(coordinator.fixed_delta_seconds() - (1.0 / 120.0)) < 0.0000001);
    assert(!coordinator.set_fixed_delta_seconds(0.0));
    assert(!coordinator.set_fixed_delta_seconds(-1.0));

    MockDomain source{DomainId{1}, "source"};
    MockDomain receiver{DomainId{2}, "receiver"};
    MockDomain duplicate{DomainId{1}, "duplicate"};
    MockDomain invalid{DomainId{}, "invalid"};
    MockDomain unnamed{DomainId{3}, ""};

    assert(coordinator.register_domain(source));
    assert(coordinator.register_domain(receiver));
    assert(!coordinator.register_domain(duplicate));
    assert(!coordinator.register_domain(invalid));
    assert(!coordinator.register_domain(unnamed));

    source.emit_next_to(receiver.id());
    coordinator.step();
    assert(source.steps() == 1U);
    assert(receiver.steps() == 1U);
    assert(receiver.received().empty());
    assert(source.last_step_index() == 0U);
    assert(source.last_delta_seconds() == coordinator.fixed_delta_seconds());

    coordinator.step();
    assert(receiver.received().size() == 1U);
    const auto& interaction = receiver.received().front();
    assert(interaction.source == source.id());
    assert(interaction.target.has_value() && *interaction.target == receiver.id());
    assert(interaction.kind == "mock.contact");
    assert(interaction.payload.size() == 1U);
    assert(interaction.payload.front() == std::byte{0x2A});

    const auto snapshots = coordinator.debug_snapshots();
    assert(snapshots.size() == 2U);
    assert(snapshots[0].id == source.id());
    assert(snapshots[0].completed_steps == 2U);
    assert(snapshots[0].emitted_interactions == 1U);
    assert(snapshots[1].id == receiver.id());
    assert(snapshots[1].completed_steps == 2U);
    assert(snapshots[1].received_interactions == 1U);

    receiver.set_fidelity(Fidelity::off);
    const auto receiver_steps_before_off = receiver.steps();
    coordinator.step();
    assert(receiver.steps() == receiver_steps_before_off);

    receiver.set_fidelity(Fidelity::reduced);
    coordinator.step();
    assert(receiver.steps() == receiver_steps_before_off + 1U);

    assert(coordinator.unregister_domain(receiver.id()));
    assert(!coordinator.unregister_domain(receiver.id()));
    assert(coordinator.debug_snapshots().size() == 1U);

    return 0;
}
