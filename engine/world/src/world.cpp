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
    slot.camera.reset();
    slot.light.reset();
    slot.reflection_probe.reset();
    slot.resource_references.clear();
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
    slot.camera.reset();
    slot.light.reset();
    slot.reflection_probe.reset();
    slot.resource_references.clear();
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

bool World::set_resource_reference(const EntityId entity, const ResourceReference reference) {
    if (!alive(entity) || !valid(reference.role) || !reference.resource.valid() ||
        !resource_lookup_.contains(reference.resource)) {
        return false;
    }

    auto& references = slots_[entity.index].resource_references;
    const auto found = std::find_if(
        references.begin(),
        references.end(),
        [reference](const ResourceReference& current) {
            return current.role == reference.role;
        }
    );
    if (found != references.end()) {
        found->resource = reference.resource;
        return true;
    }

    references.push_back(reference);
    return true;
}

bool World::clear_resource_reference(const EntityId entity, const ResourceRole role) {
    if (!alive(entity) || !valid(role)) {
        return false;
    }
    auto& references = slots_[entity.index].resource_references;
    const auto found = std::find_if(
        references.begin(),
        references.end(),
        [role](const ResourceReference& reference) {
            return reference.role == role;
        }
    );
    if (found == references.end()) {
        return false;
    }
    references.erase(found);
    return true;
}

std::optional<ResourceId> World::resource_reference(
    const EntityId entity,
    const ResourceRole role
) const noexcept {
    if (!alive(entity) || !valid(role)) {
        return std::nullopt;
    }
    const auto& references = slots_[entity.index].resource_references;
    const auto found = std::find_if(
        references.begin(),
        references.end(),
        [role](const ResourceReference& reference) {
            return reference.role == role;
        }
    );
    return found != references.end() ? std::optional<ResourceId>{found->resource} : std::nullopt;
}

std::span<const ResourceReference> World::resource_references(const EntityId entity) const noexcept {
    if (!alive(entity)) {
        return {};
    }
    return slots_[entity.index].resource_references;
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
    slots_[entity.index].camera.reset();
    slots_[entity.index].light.reset();
    slots_[entity.index].reflection_probe.reset();
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

bool World::set_camera(const EntityId entity, const Camera camera_value) {
    if (!alive(entity) || !slots_[entity.index].spatial_state.has_value() || !valid(camera_value)) {
        return false;
    }
    if (camera_value.primary) {
        for (std::uint32_t index = 0; index < slots_.size(); ++index) {
            if (index == entity.index) continue;
            const auto& slot = slots_[index];
            if (slot.alive && slot.camera.has_value() && slot.camera->primary) {
                return false;
            }
        }
    }
    slots_[entity.index].camera = camera_value;
    return true;
}

bool World::clear_camera(const EntityId entity) {
    if (!alive(entity) || !slots_[entity.index].camera.has_value()) {
        return false;
    }
    slots_[entity.index].camera.reset();
    return true;
}

std::optional<Camera> World::camera(const EntityId entity) const noexcept {
    if (!alive(entity)) {
        return std::nullopt;
    }
    return slots_[entity.index].camera;
}

std::optional<EntityId> World::primary_camera() const noexcept {
    for (std::uint32_t index = 0; index < slots_.size(); ++index) {
        const auto& slot = slots_[index];
        if (slot.alive && slot.camera.has_value() && slot.camera->primary) {
            return EntityId{index, slot.generation};
        }
    }
    return std::nullopt;
}

bool World::set_light(const EntityId entity, const Light light_value) {
    if (!alive(entity) || !slots_[entity.index].spatial_state.has_value() || !valid(light_value)) {
        return false;
    }
    slots_[entity.index].light = light_value;
    return true;
}

bool World::clear_light(const EntityId entity) {
    if (!alive(entity) || !slots_[entity.index].light.has_value()) return false;
    slots_[entity.index].light.reset();
    return true;
}

std::optional<Light> World::light(const EntityId entity) const noexcept {
    if (!alive(entity)) return std::nullopt;
    return slots_[entity.index].light;
}

bool World::set_reflection_probe(const EntityId entity, const ReflectionProbe probe) {
    if (!alive(entity) || !slots_[entity.index].spatial_state.has_value() || !valid(probe)) {
        return false;
    }
    slots_[entity.index].reflection_probe = probe;
    return true;
}

bool World::clear_reflection_probe(const EntityId entity) {
    if (!alive(entity) || !slots_[entity.index].reflection_probe.has_value()) return false;
    slots_[entity.index].reflection_probe.reset();
    return true;
}

std::optional<ReflectionProbe> World::reflection_probe(const EntityId entity) const noexcept {
    if (!alive(entity)) return std::nullopt;
    return slots_[entity.index].reflection_probe;
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
