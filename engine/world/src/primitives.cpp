#include <vice/world/world.hpp>

#include <algorithm>
#include <utility>
#include <unordered_set>

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

bool validate_distance_links(const WorldSnapshot& snapshot, std::string* diagnostics) {
    if (diagnostics) diagnostics->clear();
    if (!std::any_of(snapshot.entities.begin(), snapshot.entities.end(), [](const auto& e) { return e.distance_link.has_value(); })) return true;
    std::unordered_map<PersistentId, const SnapshotEntity*, PersistentIdHash> entities;
    for (const auto& entity : snapshot.entities) entities.emplace(entity.persistent_id, &entity);
    for (const auto& owner : snapshot.entities) {
        if (!owner.distance_link) continue;
        const auto& link = *owner.distance_link;
        const auto fail = [&](const char* reason) {
            if (diagnostics) *diagnostics = "Distance link " + owner.persistent_id.to_string() + ": " + reason;
            return false;
        };
        if (!valid(link)) return fail("invalid identities, limits, spring, break threshold or evidence.");
        const auto a = entities.find(link.first), b = entities.find(link.second);
        if (a == entities.end() || b == entities.end()) return fail("missing endpoint entity.");
        if (owner.persistent_id == link.first || owner.persistent_id == link.second) return fail("owner cannot be its own endpoint.");
        if (link.broken) continue;
        if (!a->second->rigid_box || !b->second->rigid_box || !a->second->spatial_state || !b->second->spatial_state)
            return fail("intact endpoints require spatial rigid boxes.");
        if (a->second->spatial_state->region != b->second->spatial_state->region) return fail("intact endpoints must share a physical region.");
        if (a->second->rigid_box->motion != RigidMotion::dynamic_body && b->second->rigid_box->motion != RigidMotion::dynamic_body)
            return fail("at least one intact endpoint must be dynamic.");
    }
    if (diagnostics) diagnostics->clear();
    return true;
}

bool World::register_component_type(ComponentTypeDesc type) {
    if (!valid_component_type(type) ||
        component_types_.contains(type.id)||component_types_.size()>=65536) {
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

    std::sort(type.fields.begin(),type.fields.end(),[](const auto& a,const auto& b){return a.id<b.id;});
    component_types_.emplace(type.id, std::move(type));
    return true;
}

bool World::unregister_component_type(const ComponentTypeId id) {
    if(component_users_.contains(id))return false;
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
    const bool material_reference=std::any_of(materials_.begin(),materials_.end(),[&](const auto& m){return m.second.render_material==id;});
    const bool presentation_reference=std::any_of(slots_.begin(),slots_.end(),[&](const auto& s){return s.alive&&((s.animation&&s.animation->rig==id)||(s.audio_emitter&&s.audio_emitter->sound==id));});
    if (referenced || material_reference || presentation_reference) {
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

WorldSnapshot World::snapshot(ComponentPayloadSnapshot payloads) const {
    WorldSnapshot output;
    output.component_payloads_included=payloads==ComponentPayloadSnapshot::include;

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
            .camera = slot.camera,
            .light = slot.light,
            .reflection_probe = slot.reflection_probe,
            .rigid_box = slot.rigid_box,
            .distance_link = slot.distance_link,
            .dependency = slot.dependency,
            .character = slot.character,
            .vehicle = slot.vehicle,
            .character_attachment = slot.character_attachment,
            .animation=slot.animation,.rig_part=slot.rig_part,.audio_emitter=slot.audio_emitter,
            .material = slot.material,
            .resource_references = slot.resource_references,
            .components = output.component_payloads_included?slot.components:std::vector<ComponentPayload>{},
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

    if(payloads!=ComponentPayloadSnapshot::render){
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
            .components=output.component_payloads_included?relation_components({*subject_id,relation.kind,*object_id}):std::vector<ComponentPayload>{},
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

    }
    for(const auto& [id,m]:materials_) { (void)id;output.materials.push_back(m); }
    std::sort(output.materials.begin(),output.materials.end(),[](const auto& a,const auto& b){return a.id.bytes()<b.id.bytes();});
    if (payloads != ComponentPayloadSnapshot::render)
        output.causal_history = causal_snapshot();
    output.clock=clock_;
    return output;
}

bool World::restore(const WorldSnapshot& snapshot_data) {
    if(!snapshot_data.component_payloads_included||!valid(snapshot_data.clock))return false;
    if (!validate_presentation(snapshot_data))return false;
    if (!validate_character_attachments(snapshot_data)) return false;
    if (!validate_vehicles(snapshot_data)) return false;
    if (!validate_distance_links(snapshot_data) || !validate_dependencies(snapshot_data) || !validate_characters(snapshot_data) || !validate_causal_history(snapshot_data.causal_history)) return false;
    if(!validate_materials(snapshot_data)) return false;
    World restored;
    restored.clock_=snapshot_data.clock;if(restored.clock_.input_fraction==0)restored.clock_.input_fraction=0;

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

    for(const auto& m:snapshot_data.materials) if(!restored.set_material(m))return false;

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
        if(entity_record.components.size()>max_entity_components)return false;
        std::unordered_set<ComponentTypeId,ComponentTypeIdHash> seen_components;
        for(const auto& payload:entity_record.components) {
            if(!seen_components.insert(payload.type).second||!restored.set_component(*entity,payload))return false;
        }
        if (entity_record.spatial_state.has_value() &&
            !restored.set_spatial_state(*entity, *entity_record.spatial_state)) {
            return false;
        }
        if (entity_record.authored_transform.has_value() &&
            !restored.set_authored_transform(*entity, *entity_record.authored_transform)) {
            return false;
        }
        if (entity_record.camera.has_value() &&
            !restored.set_camera(*entity, *entity_record.camera)) {
            return false;
        }
        if (entity_record.light.has_value() &&
            !restored.set_light(*entity, *entity_record.light)) {
            return false;
        }
        if (entity_record.reflection_probe.has_value() &&
            !restored.set_reflection_probe(*entity, *entity_record.reflection_probe)) {
            return false;
        }
        if (entity_record.character && !restored.set_character(*entity, *entity_record.character)) return false;
        if (entity_record.rigid_box && !restored.set_rigid_box(*entity, *entity_record.rigid_box)) return false;
        if(entity_record.vehicle && !restored.set_vehicle(*entity,*entity_record.vehicle))return false;
        if(!restored.set_entity_material(*entity,entity_record.material))return false;
        for (const auto reference : entity_record.resource_references) {
            if (!restored.set_resource_reference(*entity, reference)) {
                return false;
            }
        }
    }

    for(const auto& e:snapshot_data.entities) {
        const auto owner=*restored.find(e.persistent_id);
        if(e.animation && !restored.set_animation(owner,*e.animation))return false;
        if(e.audio_emitter && !restored.set_audio_emitter(owner,*e.audio_emitter))return false;
    }
    for(const auto& e:snapshot_data.entities)if(e.rig_part && !restored.set_rig_part(*restored.find(e.persistent_id),*e.rig_part))return false;
    for (const auto& e:snapshot_data.entities) if(e.character_attachment && !restored.set_character_attachment(*restored.find(e.persistent_id),*e.character_attachment))return false;

    for (const auto& entity_record : snapshot_data.entities) {
        if (entity_record.distance_link &&
            !restored.set_distance_link(*restored.find(entity_record.persistent_id), *entity_record.distance_link)) return false;
    }

    for (const auto& e : snapshot_data.entities) {
        if (e.dependency) restored.install_dependency(*restored.find(e.persistent_id), *e.dependency);
    }

    for (const auto& relation_record : snapshot_data.relations) {
        const auto subject = restored.find(relation_record.subject);
        const auto object = restored.find(relation_record.object);
        if (!subject.has_value() || !object.has_value() || relation_record.kind.is_nil() ||
            !restored.add_relation(Relation{*subject, relation_record.kind, *object})) {
            return false;
        }
        if(relation_record.components.size()>max_entity_components)return false;
        std::unordered_set<ComponentTypeId,ComponentTypeIdHash> seen;
        for(const auto& p:relation_record.components)if(!seen.insert(p.type).second||!restored.set_relation_component({relation_record.subject,relation_record.kind,relation_record.object},p))return false;
    }

    if(!restored.advance_relation_revision())return false;
    if(!restored.advance_material_revision() || !restored.advance_vehicle_revision() || !restored.advance_attachment_revision() || !restored.advance_presentation_revision())return false;
    restored.causal_id_=snapshot_data.causal_history.id;
    restored.next_causal_sequence_=snapshot_data.causal_history.next_sequence;
    for (auto event : snapshot_data.causal_history.events) {
        if (event.world_ticks > restored.clock_.ticks)
            return false;
        if (event.data) {
            const auto type = restored.component_types_.find(event.data->payload.type);
            if (type == restored.component_types_.end() ||
                !valid_component_payload(event.data->payload, type->second))
                return false;
            auto &fs = event.data->payload.fields;
            std::sort(fs.begin(), fs.end(),
                      [](const auto &a, const auto &b) { return a.id < b.id; });
            for (auto &f : fs)
                if (auto n = std::get_if<double>(&f.value); n && *n == 0)
                    *n = 0;
            ++restored.component_users_[event.data->payload.type];
        }
        restored.causal_events_.push_back(std::move(event));
    }
    *this = std::move(restored);
    return true;
}

} // namespace vice::world
