#pragma once

#include <vice/simulation/simulation.hpp>
#include <vice/world/entity.hpp>

#include <cstddef>
#include <optional>
#include <span>
#include <vector>

namespace vice::world {
class World;
}

namespace vice::simulation {

struct EntityBinding {
    world::EntityId entity{};
    DomainId domain{};
    ParticipantId participant{};
};

class EntityBindings {
public:
    bool bind(
        const world::World& world,
        const Coordinator& coordinator,
        world::EntityId entity,
        DomainId domain,
        ParticipantId participant
    );

    [[nodiscard]] std::optional<ParticipantId> find(
        const world::World& world,
        const Coordinator& coordinator,
        world::EntityId entity,
        DomainId domain
    ) const noexcept;

    bool unbind(world::EntityId entity, DomainId domain);
    std::size_t remove_entity(world::EntityId entity);
    std::size_t remove_domain(DomainId domain);
    std::size_t prune(const world::World& world, const Coordinator& coordinator);

    [[nodiscard]] std::span<const EntityBinding> bindings() const noexcept {
        return bindings_;
    }

private:
    std::vector<EntityBinding> bindings_;
};

} // namespace vice::simulation
