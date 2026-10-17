#include <vice/world/world.hpp>

#include <algorithm>

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
    slot.spatial_state.reset();
    slot.authored_transform.reset();
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
    slot.spatial_state.reset();
    slot.authored_transform.reset();
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

bool World::register_region(const RegionDesc region) {
    if (!region.id.valid() || !region.simulation_island.valid() || !finite(region.origin) ||
        (region.bounds.has_value() && !valid(*region.bounds))) {
        return false;
    }
    return regions_.emplace(region.id, region).second;
}

bool World::remove_region(const RegionId region_id) {
    const auto found = regions_.find(region_id);
    if (found == regions_.end()) {
        return false;
    }

    const bool in_use = std::any_of(slots_.begin(), slots_.end(), [region_id](const Slot& slot) {
        return slot.alive && slot.spatial_state.has_value() && slot.spatial_state->region == region_id;
    });
    if (in_use) {
        return false;
    }

    regions_.erase(found);
    return true;
}

bool World::set_region_fidelity(const RegionId region_id, const RegionFidelity fidelity) {
    const auto found = regions_.find(region_id);
    if (found == regions_.end()) {
        return false;
    }
    found->second.fidelity = fidelity;
    return true;
}

std::optional<RegionDesc> World::region(const RegionId region_id) const noexcept {
    const auto found = regions_.find(region_id);
    if (found == regions_.end()) {
        return std::nullopt;
    }
    return found->second;
}

std::optional<RegionId> World::region_containing(const GlobalPosition position) const noexcept {
    if (!finite(position)) {
        return std::nullopt;
    }

    std::optional<RegionId> selected;
    for (const auto& entry : regions_) {
        if (!contains(entry.second, position)) {
            continue;
        }
        if (!selected.has_value() || entry.first.value < selected->value) {
            selected = entry.first;
        }
    }
    return selected;
}

bool World::set_spatial_state(const EntityId entity, const SpatialState state) {
    if (!alive(entity) || !state.region.valid() || !finite(state.global_position) ||
        !regions_.contains(state.region)) {
        return false;
    }

    slots_[entity.index].spatial_state = state;
    return true;
}

bool World::clear_spatial_state(const EntityId entity) {
    if (!alive(entity) || !slots_[entity.index].spatial_state.has_value()) {
        return false;
    }
    slots_[entity.index].spatial_state.reset();
    slots_[entity.index].authored_transform.reset();
    return true;
}

bool World::set_local_position(const EntityId entity, const LocalPosition position) {
    if (!alive(entity) || !finite(position)) {
        return false;
    }

    auto& slot = slots_[entity.index];
    if (!slot.spatial_state.has_value()) {
        return false;
    }

    const auto found = regions_.find(slot.spatial_state->region);
    if (found == regions_.end()) {
        return false;
    }

    const auto global = to_global_position(position, found->second.origin);
    if (!global.has_value()) {
        return false;
    }

    slot.spatial_state->global_position = *global;
    return true;
}

bool World::handoff_spatial(const EntityId entity, const RegionId target_region) {
    if (!alive(entity)) {
        return false;
    }

    auto& slot = slots_[entity.index];
    if (!slot.spatial_state.has_value() || !regions_.contains(target_region)) {
        return false;
    }

    slot.spatial_state->region = target_region;
    return true;
}

bool World::set_authored_transform(const EntityId entity, const AuthoredTransform transform) {
    if (!alive(entity) || !slots_[entity.index].spatial_state.has_value() || !valid(transform)) {
        return false;
    }
    slots_[entity.index].authored_transform = transform;
    return true;
}

bool World::clear_authored_transform(const EntityId entity) {
    if (!alive(entity) || !slots_[entity.index].authored_transform.has_value()) {
        return false;
    }
    slots_[entity.index].authored_transform.reset();
    return true;
}

std::optional<SpatialState> World::spatial_state(const EntityId entity) const noexcept {
    if (!alive(entity)) {
        return std::nullopt;
    }
    return slots_[entity.index].spatial_state;
}

std::optional<AuthoredTransform> World::authored_transform(const EntityId entity) const noexcept {
    if (!alive(entity)) {
        return std::nullopt;
    }
    return slots_[entity.index].authored_transform;
}

std::optional<LocalPosition> World::local_position(const EntityId entity) const noexcept {
    const auto state = spatial_state(entity);
    if (!state.has_value()) {
        return std::nullopt;
    }

    const auto found = regions_.find(state->region);
    if (found == regions_.end()) {
        return std::nullopt;
    }
    return to_local_position(state->global_position, found->second.origin);
}

std::optional<SimulationIslandId> World::simulation_island(const EntityId entity) const noexcept {
    const auto state = spatial_state(entity);
    if (!state.has_value()) {
        return std::nullopt;
    }

    const auto found = regions_.find(state->region);
    if (found == regions_.end()) {
        return std::nullopt;
    }
    return found->second.simulation_island;
}

std::optional<StreamingCellId> World::streaming_cell(const EntityId entity) const noexcept {
    const auto state = spatial_state(entity);
    if (!state.has_value()) {
        return std::nullopt;
    }

    const auto found = regions_.find(state->region);
    if (found == regions_.end()) {
        return std::nullopt;
    }
    return found->second.streaming_cell;
}

} // namespace vice::world
