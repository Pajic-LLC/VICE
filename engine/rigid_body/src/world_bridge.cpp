#include <vice/rigid_body/world_bridge.hpp>

#include <algorithm>

namespace vice::rigid_body {
namespace {

[[nodiscard]] world::LocalPosition local_position(const math::Vec3 value) noexcept {
    return world::LocalPosition{value.x, value.y, value.z};
}

[[nodiscard]] math::Vec3 math_position(const world::LocalPosition value) noexcept {
    return math::Vec3{value.x, value.y, value.z};
}

} // namespace

WorldBridge::RegionDomain* WorldBridge::find_region(const world::RegionId region) noexcept {
    const auto found = std::find_if(
        regions_.begin(),
        regions_.end(),
        [region](const RegionDomain& entry) {
            return entry.region == region;
        }
    );
    return found != regions_.end() ? &*found : nullptr;
}

const WorldBridge::RegionDomain* WorldBridge::find_region(
    const world::RegionId region
) const noexcept {
    const auto found = std::find_if(
        regions_.begin(),
        regions_.end(),
        [region](const RegionDomain& entry) {
            return entry.region == region;
        }
    );
    return found != regions_.end() ? &*found : nullptr;
}

WorldBridge::BoxRecord* WorldBridge::find_record(const world::EntityId entity) noexcept {
    const auto found = std::find_if(
        boxes_.begin(),
        boxes_.end(),
        [entity](const BoxRecord& entry) {
            return entry.entity == entity;
        }
    );
    return found != boxes_.end() ? &*found : nullptr;
}

const WorldBridge::BoxRecord* WorldBridge::find_record(
    const world::EntityId entity
) const noexcept {
    const auto found = std::find_if(
        boxes_.begin(),
        boxes_.end(),
        [entity](const BoxRecord& entry) {
            return entry.entity == entity;
        }
    );
    return found != boxes_.end() ? &*found : nullptr;
}

bool WorldBridge::register_region(
    const world::World& world,
    const simulation::Coordinator& coordinator,
    const world::RegionId region_id,
    Domain& domain
) {
    const auto region_desc = world.region(region_id);
    if (!region_desc.has_value() || !coordinator.registered(domain.id())) {
        return false;
    }

    const auto duplicate = std::find_if(
        regions_.begin(),
        regions_.end(),
        [&](const RegionDomain& entry) {
            return entry.region == region_id ||
                entry.island == region_desc->simulation_island ||
                entry.domain == &domain ||
                entry.domain->id() == domain.id();
        }
    );
    if (duplicate != regions_.end()) {
        return false;
    }

    regions_.push_back(RegionDomain{
        .region = region_id,
        .island = region_desc->simulation_island,
        .domain = &domain,
    });
    return true;
}

bool WorldBridge::unregister_region(const world::RegionId region) noexcept {
    const auto found = std::find_if(
        regions_.begin(),
        regions_.end(),
        [region](const RegionDomain& entry) {
            return entry.region == region;
        }
    );
    if (found == regions_.end()) {
        return false;
    }
    regions_.erase(found);
    return true;
}

bool WorldBridge::create_box(
    world::World& world,
    const simulation::Coordinator& coordinator,
    simulation::EntityBindings& bindings,
    const world::EntityId entity,
    BoxBodyDesc desc
) {
    if (!world.alive(entity) || !desc.participant || find_record(entity) != nullptr) {
        return false;
    }

    const auto spatial = world.spatial_state(entity);
    if (!spatial.has_value()) {
        return false;
    }

    auto* registration = find_region(spatial->region);
    const auto local = world.local_position(entity);
    if (registration == nullptr || local == std::nullopt ||
        !coordinator.registered(registration->domain->id())) {
        return false;
    }

    for (const auto& entry : regions_) {
        if (bindings.find(world, coordinator, entity, entry.domain->id()).has_value()) {
            return false;
        }
    }

    desc.position = math_position(*local);
    if (!registration->domain->create_box(desc)) {
        return false;
    }

    if (!bindings.bind(
        world,
        coordinator,
        entity,
        registration->domain->id(),
        desc.participant
    )) {
        (void)registration->domain->destroy_body(desc.participant);
        return false;
    }

    try {
        boxes_.push_back(BoxRecord{
            .entity = entity,
            .participant = desc.participant,
            .creation = desc,
        });
    } catch (...) {
        (void)bindings.unbind(entity, registration->domain->id());
        (void)registration->domain->destroy_body(desc.participant);
        throw;
    }
    return true;
}

bool WorldBridge::sync_body_to_world(
    world::World& world,
    const simulation::Coordinator& coordinator,
    const simulation::EntityBindings& bindings,
    const world::EntityId entity
) const {
    if (!world.alive(entity)) {
        return false;
    }

    const auto spatial = world.spatial_state(entity);
    if (!spatial.has_value()) {
        return false;
    }

    const auto* registration = find_region(spatial->region);
    if (registration == nullptr || !coordinator.registered(registration->domain->id())) {
        return false;
    }

    const auto participant = bindings.find(
        world,
        coordinator,
        entity,
        registration->domain->id()
    );
    if (!participant.has_value()) {
        return false;
    }

    const auto state = registration->domain->body_state(*participant);
    if (!state.has_value()) {
        return false;
    }

    return world.set_local_position(entity, local_position(state->position));
}

bool WorldBridge::handoff_box(
    world::World& world,
    const simulation::Coordinator& coordinator,
    simulation::EntityBindings& bindings,
    const world::EntityId entity,
    const world::RegionId target_region
) {
    if (!world.alive(entity)) {
        return false;
    }

    const auto previous_spatial = world.spatial_state(entity);
    const auto target_desc = world.region(target_region);
    auto* record = find_record(entity);
    if (!previous_spatial.has_value() || !target_desc.has_value() || record == nullptr ||
        previous_spatial->region == target_region) {
        return false;
    }

    auto* source = find_region(previous_spatial->region);
    auto* target = find_region(target_region);
    if (source == nullptr || target == nullptr || source->domain == target->domain ||
        source->island == target->island ||
        !coordinator.registered(source->domain->id()) ||
        !coordinator.registered(target->domain->id())) {
        return false;
    }

    const auto participant = bindings.find(
        world,
        coordinator,
        entity,
        source->domain->id()
    );
    if (!participant.has_value() || *participant != record->participant ||
        bindings.find(world, coordinator, entity, target->domain->id()).has_value()) {
        return false;
    }

    const auto source_state = source->domain->body_state(*participant);
    const auto source_region = world.region(source->region);
    if (!source_state.has_value() || !source_region.has_value()) {
        return false;
    }

    const auto live_global = world::to_global_position(
        local_position(source_state->position),
        source_region->origin
    );
    if (!live_global.has_value()) {
        return false;
    }

    const auto target_local = world::to_local_position(*live_global, target_desc->origin);
    if (!target_local.has_value()) {
        return false;
    }

    BoxBodyDesc recreated = record->creation;
    recreated.position = math_position(*target_local);
    recreated.orientation = source_state->orientation;
    recreated.linear_velocity = source_state->linear_velocity;
    recreated.angular_velocity = source_state->angular_velocity;

    if (!target->domain->create_box(recreated)) {
        return false;
    }

    if (recreated.motion == MotionType::dynamic_body && !source_state->active &&
        !target->domain->set_body_active(*participant, false)) {
        (void)target->domain->destroy_body(*participant);
        return false;
    }

    if (!bindings.bind(
        world,
        coordinator,
        entity,
        target->domain->id(),
        *participant
    )) {
        (void)target->domain->destroy_body(*participant);
        return false;
    }

    if (!world.set_spatial_state(entity, world::SpatialState{
        .global_position = *live_global,
        .region = target_region,
    })) {
        (void)bindings.unbind(entity, target->domain->id());
        (void)target->domain->destroy_body(*participant);
        return false;
    }

    if (!bindings.unbind(entity, source->domain->id())) {
        (void)world.set_spatial_state(entity, *previous_spatial);
        (void)bindings.unbind(entity, target->domain->id());
        (void)target->domain->destroy_body(*participant);
        return false;
    }

    if (!source->domain->destroy_body(*participant)) {
        (void)world.set_spatial_state(entity, *previous_spatial);
        (void)bindings.unbind(entity, target->domain->id());
        (void)target->domain->destroy_body(*participant);
        (void)bindings.bind(
            world,
            coordinator,
            entity,
            source->domain->id(),
            *participant
        );
        return false;
    }

    record->creation.position = recreated.position;
    record->creation.orientation = recreated.orientation;
    record->creation.linear_velocity = recreated.linear_velocity;
    record->creation.angular_velocity = recreated.angular_velocity;
    return true;
}

} // namespace vice::rigid_body
