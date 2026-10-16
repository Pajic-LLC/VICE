#include <vice/simulation/entity_bindings.hpp>

#include <vice/world/world.hpp>

#include <algorithm>

namespace vice::simulation {

bool EntityBindings::bind(
    const world::World& world,
    const Coordinator& coordinator,
    const world::EntityId entity,
    const DomainId domain,
    const ParticipantId participant
) {
    if (!world.alive(entity) || !coordinator.registered(domain) || !participant) {
        return false;
    }

    const auto duplicate_entity_domain = std::find_if(
        bindings_.begin(),
        bindings_.end(),
        [&](const EntityBinding& binding) {
            return binding.entity == entity && binding.domain == domain;
        }
    );
    if (duplicate_entity_domain != bindings_.end()) {
        return false;
    }

    const auto duplicate_participant = std::find_if(
        bindings_.begin(),
        bindings_.end(),
        [&](const EntityBinding& binding) {
            return binding.domain == domain && binding.participant == participant;
        }
    );
    if (duplicate_participant != bindings_.end()) {
        return false;
    }

    bindings_.push_back(EntityBinding{
        .entity = entity,
        .domain = domain,
        .participant = participant,
    });
    return true;
}

std::optional<ParticipantId> EntityBindings::find(
    const world::World& world,
    const Coordinator& coordinator,
    const world::EntityId entity,
    const DomainId domain
) const noexcept {
    if (!world.alive(entity) || !coordinator.registered(domain)) {
        return std::nullopt;
    }

    const auto found = std::find_if(
        bindings_.begin(),
        bindings_.end(),
        [&](const EntityBinding& binding) {
            return binding.entity == entity && binding.domain == domain;
        }
    );
    if (found == bindings_.end()) {
        return std::nullopt;
    }
    return found->participant;
}

bool EntityBindings::unbind(const world::EntityId entity, const DomainId domain) {
    const auto found = std::find_if(
        bindings_.begin(),
        bindings_.end(),
        [&](const EntityBinding& binding) {
            return binding.entity == entity && binding.domain == domain;
        }
    );
    if (found == bindings_.end()) {
        return false;
    }
    bindings_.erase(found);
    return true;
}

std::size_t EntityBindings::remove_entity(const world::EntityId entity) {
    const auto old_size = bindings_.size();
    std::erase_if(
        bindings_,
        [&](const EntityBinding& binding) {
            return binding.entity == entity;
        }
    );
    return old_size - bindings_.size();
}

std::size_t EntityBindings::remove_domain(const DomainId domain) {
    const auto old_size = bindings_.size();
    std::erase_if(
        bindings_,
        [&](const EntityBinding& binding) {
            return binding.domain == domain;
        }
    );
    return old_size - bindings_.size();
}

std::size_t EntityBindings::prune(
    const world::World& world,
    const Coordinator& coordinator
) {
    const auto old_size = bindings_.size();
    std::erase_if(
        bindings_,
        [&](const EntityBinding& binding) {
            return !world.alive(binding.entity) || !coordinator.registered(binding.domain);
        }
    );
    return old_size - bindings_.size();
}

} // namespace vice::simulation
