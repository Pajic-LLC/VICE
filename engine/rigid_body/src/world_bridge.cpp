#include <vice/rigid_body/world_bridge.hpp>

#include <algorithm>
#include <cstdint>
#include <limits>

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

WorldBridge::CharacterRecord* WorldBridge::find_character_record(
    const world::PersistentId owner
) noexcept {
    const auto found = std::find_if(
        characters_.begin(),
        characters_.end(),
        [&owner](const CharacterRecord& entry) {
            return entry.owner == owner;
        }
    );
    return found != characters_.end() ? &*found : nullptr;
}

const WorldBridge::CharacterRecord* WorldBridge::find_character_record(
    const world::PersistentId owner
) const noexcept {
    const auto found = std::find_if(
        characters_.begin(),
        characters_.end(),
        [&owner](const CharacterRecord& entry) {
            return entry.owner == owner;
        }
    );
    return found != characters_.end() ? &*found : nullptr;
}

std::optional<CharacterId> WorldBridge::available_character_id(
    const Domain& domain,
    CharacterId preferred
) noexcept {
    constexpr std::uint32_t native_invalid = std::numeric_limits<std::uint32_t>::max();
    if (!preferred || preferred.value == native_invalid) {
        preferred = CharacterId{1};
    }

    CharacterId candidate = preferred;
    const std::size_t attempts = domain.character_count() + 1U;
    for (std::size_t attempt = 0; attempt < attempts; ++attempt) {
        if (!domain.character_state(candidate).has_value()) {
            return candidate;
        }
        candidate.value = candidate.value >= native_invalid - 1U
            ? 1U
            : candidate.value + 1U;
    }
    return std::nullopt;
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
    recreated.motion_quality = source_state->motion_quality;

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
    record->creation.motion_quality = recreated.motion_quality;
    return true;
}

bool WorldBridge::create_character(
    world::World& world,
    const simulation::Coordinator& coordinator,
    const world::EntityId entity,
    CharacterDesc desc
) {
    if (!world.alive(entity)) {
        return false;
    }

    const auto owner = world.persistent_id(entity);
    const auto spatial = world.spatial_state(entity);
    const auto local = world.local_position(entity);
    if (!owner.has_value() || owner->is_nil() || !spatial.has_value() || !local.has_value() ||
        find_character_record(*owner) != nullptr) {
        return false;
    }

    auto* registration = find_region(spatial->region);
    if (registration == nullptr || !coordinator.registered(registration->domain->id())) {
        return false;
    }

    const auto local_id = available_character_id(*registration->domain, desc.id);
    if (!local_id.has_value()) {
        return false;
    }

    desc.id = *local_id;
    desc.position = math_position(*local);
    if (!registration->domain->create_character(desc)) {
        return false;
    }

    try {
        characters_.push_back(CharacterRecord{
            .owner = *owner,
            .character = *local_id,
            .creation = desc,
        });
    } catch (...) {
        (void)registration->domain->destroy_character(*local_id);
        throw;
    }
    return true;
}

bool WorldBridge::remove_character(world::World& world,const simulation::Coordinator& coordinator,world::EntityId entity) {
    const auto id=character_id(world,coordinator,entity);const auto spatial=world.spatial_state(entity);const auto owner=world.persistent_id(entity);
    auto* region=spatial?find_region(spatial->region):nullptr;
    if(!id || !region || !owner || !region->domain->destroy_character(*id))return false;
    std::erase_if(characters_,[&](const auto& record){return record.owner==*owner;});remove_activation_requests(*owner);return true;
}

std::optional<CharacterId> WorldBridge::character_id(
    const world::World& world,
    const simulation::Coordinator& coordinator,
    const world::EntityId entity
) const noexcept {
    if (!world.alive(entity)) {
        return std::nullopt;
    }

    const auto owner = world.persistent_id(entity);
    const auto spatial = world.spatial_state(entity);
    if (!owner.has_value() || !spatial.has_value()) {
        return std::nullopt;
    }

    const auto* record = find_character_record(*owner);
    const auto* registration = find_region(spatial->region);
    if (record == nullptr || registration == nullptr ||
        !coordinator.registered(registration->domain->id()) ||
        !registration->domain->character_state(record->character).has_value()) {
        return std::nullopt;
    }
    return record->character;
}

bool WorldBridge::sync_character_to_world(
    world::World& world,
    const simulation::Coordinator& coordinator,
    const world::EntityId entity
) const {
    const auto local_id = character_id(world, coordinator, entity);
    const auto spatial = world.spatial_state(entity);
    if (!local_id.has_value() || !spatial.has_value()) {
        return false;
    }

    const auto* registration = find_region(spatial->region);
    if (registration == nullptr) {
        return false;
    }

    const auto state = registration->domain->character_state(*local_id);
    if (!state.has_value()) {
        return false;
    }
    return world.set_local_position(entity, local_position(state->position));
}

bool WorldBridge::handoff_character(
    world::World& world,
    const simulation::Coordinator& coordinator,
    const world::EntityId entity,
    const world::RegionId target_region
) {
    if (!world.alive(entity)) {
        return false;
    }

    const auto owner = world.persistent_id(entity);
    const auto previous_spatial = world.spatial_state(entity);
    const auto target_desc = world.region(target_region);
    if (!owner.has_value() || !previous_spatial.has_value() || !target_desc.has_value() ||
        previous_spatial->region == target_region) {
        return false;
    }

    auto* record = find_character_record(*owner);
    auto* source = find_region(previous_spatial->region);
    auto* target = find_region(target_region);
    if (record == nullptr || source == nullptr || target == nullptr ||
        source->domain == target->domain || source->island == target->island ||
        !coordinator.registered(source->domain->id()) ||
        !coordinator.registered(target->domain->id())) {
        return false;
    }

    const auto source_state = source->domain->character_state(record->character);
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

    const auto target_id = available_character_id(*target->domain, record->character);
    if (!target_id.has_value()) {
        return false;
    }

    CharacterDesc recreated = record->creation;
    recreated.id = *target_id;
    recreated.position = math_position(*target_local);
    if (!target->domain->create_character(recreated)) {
        return false;
    }

    const auto rollback_target = [&]() noexcept {
        (void)target->domain->destroy_character(*target_id);
    };

    if (!target->domain->set_character_velocity(*target_id, source_state->velocity) ||
        !target->domain->set_character_horizontal_velocity(
            *target_id,
            source_state->horizontal_velocity
        )) {
        rollback_target();
        return false;
    }
    if (source_state->pending_jump_speed.has_value() &&
        !target->domain->request_character_jump(*target_id, *source_state->pending_jump_speed)) {
        rollback_target();
        return false;
    }

    if (!world.set_spatial_state(entity, world::SpatialState{
        .global_position = *live_global,
        .region = target_region,
    })) {
        rollback_target();
        return false;
    }

    if (!source->domain->destroy_character(record->character)) {
        (void)world.set_spatial_state(entity, *previous_spatial);
        rollback_target();
        return false;
    }

    record->character = *target_id;
    record->creation.id = *target_id;
    record->creation.position = recreated.position;
    return true;
}

} // namespace vice::rigid_body
