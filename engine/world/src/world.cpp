#include <vice/world/world.hpp>

#include <algorithm>
#include <limits>

namespace vice::world {

EntityId World::create_entity() {
    auto persistent_id = PersistentId::random();
    while (persistent_lookup_.contains(persistent_id)) {
        persistent_id = PersistentId::random();
    }
    return allocate(persistent_id);
}

std::optional<EntityId> World::create_entity(const PersistentId persistent_id) {
    if (persistent_id.is_nil() || persistent_lookup_.contains(persistent_id)) {
        return std::nullopt;
    }
    return allocate(persistent_id);
}

EntityId World::allocate(const PersistentId persistent_id) {
    std::uint32_t index = 0;

    if (!free_indices_.empty()) {
        index = free_indices_.back();
        free_indices_.pop_back();
    } else {
        index = static_cast<std::uint32_t>(slots_.size());
        slots_.push_back(Slot{});
    }

    auto& slot = slots_[index];
    slot.persistent_id = persistent_id;
    slot.alive = true;

    const EntityId entity{index, slot.generation};
    persistent_lookup_.emplace(persistent_id, entity);
    ++entity_count_;
    return entity;
}

bool World::destroy_entity(const EntityId entity) {
    if (!alive(entity)) {
        return false;
    }

    auto& slot = slots_[entity.index];
    persistent_lookup_.erase(slot.persistent_id);
    slot.persistent_id = PersistentId{};
    slot.alive = false;

    ++slot.generation;
    if (slot.generation == 0) {
        slot.generation = 1;
    }

    free_indices_.push_back(entity.index);
    --entity_count_;

    std::erase_if(relations_, [entity](const Relation& relation) {
        return relation.subject == entity || relation.object == entity;
    });

    return true;
}

bool World::alive(const EntityId entity) const noexcept {
    if (!entity.valid() || entity.index >= slots_.size()) {
        return false;
    }

    const auto& slot = slots_[entity.index];
    return slot.alive && slot.generation == entity.generation;
}

std::size_t World::entity_count() const noexcept {
    return entity_count_;
}

std::optional<PersistentId> World::persistent_id(const EntityId entity) const noexcept {
    if (!alive(entity)) {
        return std::nullopt;
    }
    return slots_[entity.index].persistent_id;
}

std::optional<EntityId> World::find(const PersistentId persistent_id) const noexcept {
    const auto found = persistent_lookup_.find(persistent_id);
    if (found == persistent_lookup_.end()) {
        return std::nullopt;
    }
    return found->second;
}

bool World::add_relation(const Relation relation) {
    if (!alive(relation.subject) || !alive(relation.object) || relation.kind.is_nil()) {
        return false;
    }

    if (std::find(relations_.begin(), relations_.end(), relation) != relations_.end()) {
        return false;
    }

    relations_.push_back(relation);
    return true;
}

bool World::remove_relation(const Relation& relation) {
    const auto found = std::find(relations_.begin(), relations_.end(), relation);
    if (found == relations_.end()) {
        return false;
    }

    relations_.erase(found);
    return true;
}

std::span<const Relation> World::relations() const noexcept {
    return relations_;
}

} // namespace vice::world
