#include <vice/rigid_body/world_bridge.hpp>

#include <algorithm>
#include <cmath>

namespace vice::rigid_body {
namespace {

[[nodiscard]] world::LocalPosition local_position(const math::Vec3 value) noexcept {
    return world::LocalPosition{value.x, value.y, value.z};
}

[[nodiscard]] bool persistent_less(
    const world::PersistentId& left,
    const world::PersistentId& right
) noexcept {
    return std::lexicographical_compare(
        left.bytes().begin(),
        left.bytes().end(),
        right.bytes().begin(),
        right.bytes().end()
    );
}

[[nodiscard]] world::GlobalPosition predict_position(
    const world::GlobalPosition position,
    const math::Vec3 velocity,
    const float seconds
) noexcept {
    const double horizon = static_cast<double>(seconds);
    return world::GlobalPosition{
        position.x + static_cast<double>(velocity.x) * horizon,
        position.y + static_cast<double>(velocity.y) * horizon,
        position.z + static_cast<double>(velocity.z) * horizon,
    };
}

} // namespace

void WorldBridge::remove_activation_requests(const world::PersistentId owner) noexcept {
    std::erase_if(
        region_activation_requests_,
        [&owner](const RegionActivationRequest& request) {
            return request.owner == owner;
        }
    );
}

void WorldBridge::request_region_activation(
    const world::PersistentId owner,
    const world::RegionId source_region,
    const world::RegionDesc& target
) {
    const auto found = std::find_if(
        region_activation_requests_.begin(),
        region_activation_requests_.end(),
        [&](const RegionActivationRequest& request) {
            return request.owner == owner && request.target_region == target.id;
        }
    );
    if (found != region_activation_requests_.end()) {
        found->source_region = source_region;
        found->target_island = target.simulation_island;
        found->target_cell = target.streaming_cell;
        return;
    }

    remove_activation_requests(owner);
    region_activation_requests_.push_back(RegionActivationRequest{
        .owner = owner,
        .source_region = source_region,
        .target_region = target.id,
        .target_island = target.simulation_island,
        .target_cell = target.streaming_cell,
    });
    std::sort(
        region_activation_requests_.begin(),
        region_activation_requests_.end(),
        [](const RegionActivationRequest& left, const RegionActivationRequest& right) {
            if (left.owner != right.owner) {
                return persistent_less(left.owner, right.owner);
            }
            return left.target_region.value < right.target_region.value;
        }
    );
}

void WorldBridge::orchestrate_characters(
    world::World& world,
    const simulation::Coordinator& coordinator,
    CharacterRegionOrchestrationConfig config
) {
    character_region_events_.clear();
    if (!std::isfinite(config.prefetch_seconds) || config.prefetch_seconds < 0.0F) {
        config.prefetch_seconds = 0.0F;
    }

    std::vector<world::PersistentId> owners;
    owners.reserve(characters_.size());
    for (const auto& record : characters_) {
        owners.push_back(record.owner);
    }
    std::sort(owners.begin(), owners.end(), persistent_less);
    owners.erase(std::unique(owners.begin(), owners.end()), owners.end());

    for (const auto& owner : owners) {
        const auto entity = world.find(owner);
        if (!entity.has_value()) {
            remove_activation_requests(owner);
            character_region_events_.push_back(CharacterRegionEvent{
                .owner = owner,
                .action = CharacterRegionAction::failed,
            });
            continue;
        }

        const auto spatial = world.spatial_state(*entity);
        auto* record = find_character_record(owner);
        if (!spatial.has_value() || record == nullptr) {
            remove_activation_requests(owner);
            character_region_events_.push_back(CharacterRegionEvent{
                .owner = owner,
                .action = CharacterRegionAction::failed,
            });
            continue;
        }

        const world::RegionId source_region_id = spatial->region;
        const auto source_desc = world.region(source_region_id);
        auto* source = find_region(source_region_id);
        if (!source_desc.has_value() || source == nullptr ||
            !coordinator.registered(source->domain->id())) {
            remove_activation_requests(owner);
            character_region_events_.push_back(CharacterRegionEvent{
                .owner = owner,
                .source_region = source_region_id,
                .action = CharacterRegionAction::failed,
            });
            continue;
        }

        const auto source_state = source->domain->character_state(record->character);
        if (!source_state.has_value()) {
            remove_activation_requests(owner);
            character_region_events_.push_back(CharacterRegionEvent{
                .owner = owner,
                .source_region = source_region_id,
                .action = CharacterRegionAction::failed,
            });
            continue;
        }

        const auto live_global = world::to_global_position(
            local_position(source_state->position),
            source_desc->origin
        );
        if (!live_global.has_value() || !world.set_spatial_state(*entity, world::SpatialState{
            .global_position = *live_global,
            .region = source_region_id,
        })) {
            character_region_events_.push_back(CharacterRegionEvent{
                .owner = owner,
                .source_region = source_region_id,
                .action = CharacterRegionAction::failed,
            });
            continue;
        }

        const bool inside_source = !source_desc->bounds.has_value() ||
            world::contains(*source_desc, *live_global);
        if (inside_source) {
            if (source_desc->bounds.has_value() && config.prefetch_seconds > 0.0F) {
                const auto predicted = predict_position(
                    *live_global,
                    source_state->velocity,
                    config.prefetch_seconds
                );
                if (!world::contains(*source_desc, predicted)) {
                    const auto predicted_target = world.region_containing(predicted);
                    if (predicted_target.has_value() && *predicted_target != source_region_id) {
                        const auto target_desc = world.region(*predicted_target);
                        if (target_desc.has_value()) {
                            auto* target = find_region(*predicted_target);
                            const bool target_ready =
                                target_desc->fidelity != world::RegionFidelity::inactive &&
                                target != nullptr && coordinator.registered(target->domain->id());
                            if (!target_ready) {
                                request_region_activation(owner, source_region_id, *target_desc);
                                character_region_events_.push_back(CharacterRegionEvent{
                                    .owner = owner,
                                    .source_region = source_region_id,
                                    .target_region = predicted_target,
                                    .action = CharacterRegionAction::activation_requested,
                                });
                                continue;
                            }
                        }
                    }
                }
            }

            remove_activation_requests(owner);
            character_region_events_.push_back(CharacterRegionEvent{
                .owner = owner,
                .source_region = source_region_id,
                .action = CharacterRegionAction::stayed,
            });
            continue;
        }

        const auto target_region = world.region_containing(*live_global);
        if (!target_region.has_value() || *target_region == source_region_id) {
            remove_activation_requests(owner);
            character_region_events_.push_back(CharacterRegionEvent{
                .owner = owner,
                .source_region = source_region_id,
                .action = CharacterRegionAction::no_target,
            });
            continue;
        }

        const auto target_desc = world.region(*target_region);
        if (!target_desc.has_value()) {
            remove_activation_requests(owner);
            character_region_events_.push_back(CharacterRegionEvent{
                .owner = owner,
                .source_region = source_region_id,
                .target_region = target_region,
                .action = CharacterRegionAction::failed,
            });
            continue;
        }

        auto* target = find_region(*target_region);
        const bool target_ready = target_desc->fidelity != world::RegionFidelity::inactive &&
            target != nullptr && coordinator.registered(target->domain->id());
        if (!target_ready) {
            request_region_activation(owner, source_region_id, *target_desc);
            character_region_events_.push_back(CharacterRegionEvent{
                .owner = owner,
                .source_region = source_region_id,
                .target_region = target_region,
                .action = CharacterRegionAction::activation_requested,
            });
            continue;
        }

        if (handoff_character(world, coordinator, *entity, *target_region)) {
            remove_activation_requests(owner);
            character_region_events_.push_back(CharacterRegionEvent{
                .owner = owner,
                .source_region = source_region_id,
                .target_region = target_region,
                .action = CharacterRegionAction::handed_off,
            });
            continue;
        }

        remove_activation_requests(owner);
        character_region_events_.push_back(CharacterRegionEvent{
            .owner = owner,
            .source_region = source_region_id,
            .target_region = target_region,
            .action = CharacterRegionAction::failed,
        });
    }
}

} // namespace vice::rigid_body
