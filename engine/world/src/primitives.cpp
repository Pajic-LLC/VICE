#include <vice/world/world.hpp>

#include <algorithm>
#include <utility>

namespace vice::world {
namespace {

[[nodiscard]] bool persistent_less(
    const PersistentId& left,
    const PersistentId& right
) noexcept {
    return std::lexicographical_compare(
        left.bytes().begin(),
        left.bytes().end(),
        right.bytes().begin(),
        right.bytes().end()
    );
}

} // namespace

bool World::register_component_type(ComponentTypeDesc type) {
    if (!type.id.valid() || type.name.empty() || type.version == 0U ||
        component_types_.contains(type.id)) {
        return false;
    }

    const bool duplicate_name = std::any_of(
        component_types_.begin(),
        component_types_.end(),
        [&](const auto& entry) {
            return entry.second.name == type.name;
        }
    );
    if (duplicate_name) {
        return false;
    }

    component_types_.emplace(type.id, std::move(type));
    return true;
}

bool World::unregister_component_type(const ComponentTypeId id) {
    return component_types_.erase(id) == 1U;
}

std::optional<ComponentTypeDesc> World::component_type(const ComponentTypeId id) const {
    const auto found = component_types_.find(id);
    if (found == component_types_.end()) {
        return std::nullopt;
    }
    return found->second;
}

std::size_t World::component_type_count() const noexcept {
    return component_types_.size();
}

ResourceHandle World::create_resource() {
    ResourceId id{PersistentId::random()};
    while (resource_lookup_.contains(id)) {
        id = ResourceId{PersistentId::random()};
    }
    return allocate_resource(id);
}

std::optional<ResourceHandle> World::create_resource(const ResourceId resource_id) {
    if (!resource_id.valid() || resource_lookup_.contains(resource_id)) {
        return std::nullopt;
    }
    return allocate_resource(resource_id);
}

ResourceHandle World::allocate_resource(const ResourceId resource_id) {
    std::uint32_t index = 0;
    if (!free_resource_indices_.empty()) {
        index = free_resource_indices_.back();
        free_resource_indices_.pop_back();
    } else {
        index = static_cast<std::uint32_t>(resource_slots_.size());
        resource_slots_.push_back(ResourceSlot{});
    }

    auto& slot = resource_slots_[index];
    slot.resource_id = resource_id;
    slot.alive = true;

    const ResourceHandle handle{index, slot.generation};
    resource_lookup_.emplace(resource_id, handle);
    ++resource_count_;
    return handle;
}

bool World::destroy_resource(const ResourceHandle resource) {
    if (!resource_alive(resource)) {
        return false;
    }

    const ResourceId id = resource_slots_[resource.index].resource_id;
    const bool referenced = std::any_of(
        slots_.begin(),
        slots_.end(),
        [id](const Slot& entity_slot) {
            return entity_slot.alive && std::any_of(
                entity_slot.resource_references.begin(),
                entity_slot.resource_references.end(),
                [id](const ResourceReference& reference) {
                    return reference.resource == id;
                }
            );
        }
    );
    if (referenced) {
        return false;
    }

    auto& slot = resource_slots_[resource.index];
    resource_lookup_.erase(slot.resource_id);
    slot.resource_id = ResourceId{};
    slot.alive = false;

    ++slot.generation;
    if (slot.generation == 0U) {
        slot.generation = 1U;
    }

    free_resource_indices_.push_back(resource.index);
    --resource_count_;
    return true;
}

bool World::resource_alive(const ResourceHandle resource) const noexcept {
    if (!resource.valid() || resource.index >= resource_slots_.size()) {
        return false;
    }
    const auto& slot = resource_slots_[resource.index];
    return slot.alive && slot.generation == resource.generation;
}

std::size_t World::resource_count() const noexcept {
    return resource_count_;
}

std::optional<ResourceId> World::resource_id(const ResourceHandle resource) const noexcept {
    if (!resource_alive(resource)) {
        return std::nullopt;
    }
    return resource_slots_[resource.index].resource_id;
}

std::optional<ResourceHandle> World::find_resource(const ResourceId resource_id) const noexcept {
    const auto found = resource_lookup_.find(resource_id);
    if (found == resource_lookup_.end()) {
        return std::nullopt;
    }
    return found->second;
}

WorldSnapshot World::snapshot() const {
    WorldSnapshot output;

    output.component_types.reserve(component_types_.size());
    for (const auto& entry : component_types_) {
        output.component_types.push_back(entry.second);
    }
    std::sort(
        output.component_types.begin(),
        output.component_types.end(),
        [](const ComponentTypeDesc& left, const ComponentTypeDesc& right) {
            return persistent_less(left.id.value, right.id.value);
        }
    );

    output.resources.reserve(resource_count_);
    for (const auto& slot : resource_slots_) {
        if (slot.alive) {
            output.resources.push_back(slot.resource_id);
        }
    }
    std::sort(
        output.resources.begin(),
        output.resources.end(),
        [](const ResourceId left, const ResourceId right) {
            return persistent_less(left.value, right.value);
        }
    );

    output.regions.reserve(regions_.size());
    for (const auto& entry : regions_) {
        output.regions.push_back(entry.second);
    }
    std::sort(
        output.regions.begin(),
        output.regions.end(),
        [](const RegionDesc& left, const RegionDesc& right) {
            return left.id.value < right.id.value;
        }
    );

    output.entities.reserve(entity_count_);
    for (const auto& slot : slots_) {
        if (!slot.alive) {
            continue;
        }
        SnapshotEntity entity{
            .persistent_id = slot.persistent_id,
            .spatial_state = slot.spatial_state,
            .authored_transform = slot.authored_transform,
            .resource_references = slot.resource_references,
        };
        std::sort(
            entity.resource_references.begin(),
            entity.resource_references.end(),
            [](const ResourceReference& left, const ResourceReference& right) {
                if (left.role != right.role) {
                    return static_cast<std::uint8_t>(left.role) < static_cast<std::uint8_t>(right.role);
                }
                return persistent_less(left.resource.value, right.resource.value);
            }
        );
        output.entities.push_back(std::move(entity));
    }
    std::sort(
        output.entities.begin(),
        output.entities.end(),
        [](const SnapshotEntity& left, const SnapshotEntity& right) {
            return persistent_less(left.persistent_id, right.persistent_id);
        }
    );

    output.relations.reserve(relations_.size());
    for (const auto& relation : relations_) {
        const auto subject_id = persistent_id(relation.subject);
        const auto object_id = persistent_id(relation.object);
        if (!subject_id.has_value() || !object_id.has_value()) {
            continue;
        }
        output.relations.push_back(SnapshotRelation{
            .subject = *subject_id,
            .kind = relation.kind,
            .object = *object_id,
        });
    }
    std::sort(
        output.relations.begin(),
        output.relations.end(),
        [](const SnapshotRelation& left, const SnapshotRelation& right) {
            if (left.subject != right.subject) {
                return persistent_less(left.subject, right.subject);
            }
            if (left.kind != right.kind) {
                return persistent_less(left.kind, right.kind);
            }
            return persistent_less(left.object, right.object);
        }
    );

    return output;
}

bool World::restore(const WorldSnapshot& snapshot_data) {
    World restored;

    for (const auto& type : snapshot_data.component_types) {
        if (!restored.register_component_type(type)) {
            return false;
        }
    }

    for (const auto resource : snapshot_data.resources) {
        if (!restored.create_resource(resource).has_value()) {
            return false;
        }
    }

    for (const auto& region_desc : snapshot_data.regions) {
        if (!restored.register_region(region_desc)) {
            return false;
        }
    }

    for (const auto& entity_record : snapshot_data.entities) {
        const auto entity = restored.create_entity(entity_record.persistent_id);
        if (!entity.has_value()) {
            return false;
        }
        if (entity_record.spatial_state.has_value() &&
            !restored.set_spatial_state(*entity, *entity_record.spatial_state)) {
            return false;
        }
        if (entity_record.authored_transform.has_value() &&
            !restored.set_authored_transform(*entity, *entity_record.authored_transform)) {
            return false;
        }
        for (const auto reference : entity_record.resource_references) {
            if (!restored.set_resource_reference(*entity, reference)) {
                return false;
            }
        }
    }

    for (const auto& relation_record : snapshot_data.relations) {
        const auto subject = restored.find(relation_record.subject);
        const auto object = restored.find(relation_record.object);
        if (!subject.has_value() || !object.has_value() || relation_record.kind.is_nil() ||
            !restored.add_relation(Relation{*subject, relation_record.kind, *object})) {
            return false;
        }
    }

    *this = std::move(restored);
    return true;
}

} // namespace vice::world
