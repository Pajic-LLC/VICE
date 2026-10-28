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
    slot.rigid_box.reset();
    slot.distance_link.reset();
    slot.dependency.reset();
    slot.character.reset();
    slot.vehicle.reset();
    slot.character_attachment.reset();
    slot.animation.reset();slot.rig_part.reset();slot.audio_emitter.reset();
    slot.material={};
    slot.resource_references.clear();
    slot.components.clear();
    slot.alive = true;

    const EntityId entity{index, slot.generation};
    persistent_lookup_.emplace(persistent_id, entity);
    ++entity_count_;
    return entity;
}

bool World::destroy_entity(const EntityId entity) {
    if (has_attached_characters(entity) || has_rig_parts(entity)) return false;
    if (alive(entity) && (distance_link_users_.contains(slots_[entity.index].persistent_id) ||
        dependency_provider_users_.contains(slots_[entity.index].persistent_id) || dependency_gate_users_.contains(slots_[entity.index].persistent_id))) return false;
    if (!alive(entity)) {
        return false;
    }

    auto& slot = slots_[entity.index];
    if(!causal_room((slot.dependency ? 2U:0U)+(slot.distance_link ? 1U:0U))) return false;
    if(!slot.material.is_nil() && !advance_material_revision())return false;
    if (!clear_animation(entity) || !clear_rig_part(entity) || !clear_audio_emitter(entity)) return false;
    if (!clear_vehicle(entity)) return false;
    (void)clear_character(entity);
    (void)clear_dependency(entity);
    (void)clear_distance_link(entity);
    if(!remove_entity_relations(slot.persistent_id))return false;
    clear_components(entity);
    persistent_lookup_.erase(slot.persistent_id);
    slot.persistent_id = PersistentId{};
    slot.spatial_state.reset();
    slot.authored_transform.reset();
    slot.camera.reset();
    slot.light.reset();
    slot.reflection_probe.reset();
    slot.rigid_box.reset();
    slot.distance_link.reset();
    slot.dependency.reset();
    slot.character.reset();
    slot.vehicle.reset();
    slot.character_attachment.reset();
    slot.animation.reset();slot.rig_part.reset();slot.audio_emitter.reset();
    slot.material={};
    slot.resource_references.clear();
    slot.components.clear();
    slot.alive = false;

    ++slot.generation;
    if (slot.generation == 0) {
        slot.generation = 1;
    }

    free_indices_.push_back(entity.index);
    --entity_count_;


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

    if(reference.role==ResourceRole::render_material) {
        const auto m=materials_.find(slots_[entity.index].material);
        if(m!=materials_.end() && m->second.render_material.valid() && m->second.render_material!=reference.resource) return false;
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
    if(role==ResourceRole::render_material) {
        const auto m=materials_.find(slots_[entity.index].material);
        if(m!=materials_.end() && m->second.render_material.valid()) return m->second.render_material;
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

    const auto& previous = slots_[entity.index].spatial_state;
    if ((!previous || previous->region != state.region) &&
        !link_endpoint_change_allowed(entity, state.region,
            slots_[entity.index].rigid_box ? std::optional{slots_[entity.index].rigid_box->motion} : std::nullopt)) return false;
    slots_[entity.index].spatial_state = state;
    return true;
}

bool World::clear_spatial_state(const EntityId entity) {
    if (has_attached_characters(entity) || has_rig_parts(entity)) return false;
    if (!alive(entity) || !slots_[entity.index].spatial_state.has_value()) {
        return false;
    }
    if (!link_endpoint_change_allowed(entity, std::nullopt, std::nullopt)) return false;
    if (!clear_animation(entity) || !clear_rig_part(entity) || !clear_audio_emitter(entity)) return false;
    if (!clear_vehicle(entity)) return false;
    slots_[entity.index].spatial_state.reset();
    slots_[entity.index].authored_transform.reset();
    slots_[entity.index].camera.reset();
    slots_[entity.index].light.reset();
    slots_[entity.index].reflection_probe.reset();
    slots_[entity.index].rigid_box.reset();
    (void)clear_character(entity);
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

bool World::set_rigid_box(const EntityId entity, const RigidBox& box) {
    if (!alive(entity) || !slots_[entity.index].spatial_state || slots_[entity.index].character || slots_[entity.index].rig_part || !valid(box)) return false;
    if (box.trigger && has_attached_characters(entity)) return false;
    if (slots_[entity.index].vehicle && (box.motion != RigidMotion::dynamic_body || box.trigger)) return false;
    const auto& previous = slots_[entity.index].rigid_box;
    if ((!previous || previous->motion != box.motion) &&
        !link_endpoint_change_allowed(entity, slots_[entity.index].spatial_state->region, box.motion)) return false;
    slots_[entity.index].rigid_box = box;
    return true;
}

bool World::clear_rigid_box(const EntityId entity) {
    if (has_attached_characters(entity)) return false;
    if (!alive(entity) || !link_endpoint_change_allowed(entity, std::nullopt, std::nullopt)) return false;
    if (!clear_vehicle(entity)) return false;
    slots_[entity.index].rigid_box.reset();
    return true;
}

std::optional<RigidBox> World::rigid_box(const EntityId entity) const noexcept {
    return alive(entity) ? slots_[entity.index].rigid_box : std::nullopt;
}

bool World::link_endpoint_change_allowed(const EntityId entity, const std::optional<RegionId> region_id,
    const std::optional<RigidMotion> motion) const {
    const auto owner = slots_[entity.index].persistent_id;
    const auto users = distance_link_users_.find(owner);
    if (users == distance_link_users_.end()) return true;
    for (const auto user : users->second) {
        const auto& link = slots_[user.index].distance_link;
        if (!link || link->broken) continue;
        if (!region_id || !motion) return false;
        const auto other = find(link->first == owner ? link->second : link->first);
        if (!other) return false;
        const auto spatial = spatial_state(*other);
        const auto body = rigid_box(*other);
        if (!spatial || !body || spatial->region != *region_id ||
            (*motion != RigidMotion::dynamic_body && body->motion != RigidMotion::dynamic_body)) return false;
    }
    return true;
}

bool World::set_distance_link(const EntityId entity,const DistanceLink& link) {
    return set_distance_link_impl(entity,link,CausalKind::distance_link_changed);
}
bool World::publish_distance_link_break(EntityId entity,float impulse,std::uint64_t solver_step) {
    const auto old=distance_link(entity);
    if(!old || old->broken || !std::isfinite(impulse) || old->break_threshold<=0 || impulse<=old->break_threshold || solver_step==0) return false;
    auto broken=*old;broken.broken=true;broken.last_break_impulse=impulse;broken.break_step=solver_step;
    return set_distance_link_impl(entity,broken,CausalKind::constraint_broken);
}
bool World::set_distance_link_impl(const EntityId entity,const DistanceLink& link,CausalKind kind) {
    if (!alive(entity) || !valid(link)) return false;
    const auto owner = slots_[entity.index].persistent_id;
    const auto first = find(link.first), second = find(link.second);
    if (!first || !second || owner == link.first || owner == link.second) return false;
    if (!link.broken) {
        const auto a = rigid_box(*first), b = rigid_box(*second);
        const auto pa = spatial_state(*first), pb = spatial_state(*second);
        if (!a || !b || !pa || !pb || pa->region != pb->region ||
            (a->motion != RigidMotion::dynamic_body && b->motion != RigidMotion::dynamic_body)) return false;
    }
    if (slots_[entity.index].distance_link == link) return true;
    auto causal_before=observe_causal_dependents(entity,false);
    if(!causal_room(causal_before.size()+1)) return false;
    const auto previous = slots_[entity.index].distance_link;
    if (previous) {
        for (const auto endpoint : {previous->first, previous->second}) {
            const auto users = distance_link_users_.find(endpoint);
            if (users == distance_link_users_.end()) continue;
            std::erase(users->second, entity);
            if (users->second.empty()) distance_link_users_.erase(users);
        }
    }
    slots_[entity.index].distance_link = link;
    ++distance_link_revision_;
    distance_link_users_[link.first].push_back(entity);
    distance_link_users_[link.second].push_back(entity);
    CausalEvent event;event.kind=kind;event.origin=kind==CausalKind::constraint_broken?CausalOrigin::physics:CausalOrigin::authoring;
    event.subject=owner;event.first=link.first;event.second=link.second;event.value=link.broken;event.region=causal_region(entity);
    if(kind==CausalKind::constraint_broken){event.quantity_kind=CausalQuantity::linear_impulse;event.quantity=link.last_break_impulse;event.solver_step=link.break_step;event.first_material=entity_material(*first);event.second_material=entity_material(*second);}
    finish_causal_change(std::move(causal_before),event);
    return true;
}

bool World::clear_distance_link(const EntityId entity) {
    if (!alive(entity) || dependency_gate_users_.contains(slots_[entity.index].persistent_id)) return false;
    auto& link = slots_[entity.index].distance_link;
    if (!link) return true;
    if(!causal_room(1)) return false;
    CausalEvent event;event.kind=CausalKind::distance_link_removed;event.origin=CausalOrigin::authoring;
    event.subject=*persistent_id(entity);event.first=link->first;event.second=link->second;event.region=causal_region(entity);
    for (const auto endpoint : {link->first, link->second}) {
        const auto users = distance_link_users_.find(endpoint);
        if (users == distance_link_users_.end()) continue;
        std::erase(users->second, entity);
        if (users->second.empty()) distance_link_users_.erase(users);
    }
    link.reset();
    ++distance_link_revision_;
    finish_causal_change({},event);
    return true;
}

std::optional<DistanceLink> World::distance_link(const EntityId entity) const noexcept {
    return alive(entity) ? slots_[entity.index].distance_link : std::nullopt;
}

bool World::handoff_spatial(const EntityId entity, const RegionId target_region) {
    if (!alive(entity)) {
        return false;
    }

    auto& slot = slots_[entity.index];
    if (!slot.spatial_state.has_value() || !regions_.contains(target_region)) {
        return false;
    }

    if (!link_endpoint_change_allowed(entity, target_region, slot.rigid_box ? std::optional{slot.rigid_box->motion} : std::nullopt)) return false;
    slot.spatial_state->region = target_region;
    return true;
}

bool World::set_authored_transform(const EntityId entity, const AuthoredTransform transform) {
    if (!alive(entity) || !slots_[entity.index].spatial_state.has_value() || !valid(transform)) {
        return false;
    }
    if(slots_[entity.index].animation && (transform.scale.x!=transform.scale.y||transform.scale.x!=transform.scale.z))return false;
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
