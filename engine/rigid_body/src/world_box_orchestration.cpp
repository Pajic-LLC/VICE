#include <vice/rigid_body/world_bridge.hpp>

#include <algorithm>
#include <cmath>

namespace vice::rigid_body {
namespace {

[[nodiscard]] world::LocalPosition box_local_position(const math::Vec3 value) noexcept {
    return world::LocalPosition{value.x, value.y, value.z};
}

[[nodiscard]] bool box_persistent_less(
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

[[nodiscard]] world::GlobalPosition predict_box_position(
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

struct BoxOwner {
    world::PersistentId owner{};
    world::EntityId entity{};
};

} // namespace

void WorldBridge::orchestrate_boxes(
    world::World& world,
    const simulation::Coordinator& coordinator,
    simulation::EntityBindings& bindings,
    BoxRegionOrchestrationConfig config
) {
    box_region_events_.clear();
    if (!std::isfinite(config.prefetch_seconds) || config.prefetch_seconds < 0.0F) {
        config.prefetch_seconds = 0.0F;
    }

    std::vector<BoxOwner> owners;
    owners.reserve(boxes_.size());
    for (const auto& record : boxes_) {
        const auto owner = world.persistent_id(record.entity);
        if (!owner.has_value() || owner->is_nil()) {
            box_region_events_.push_back(BoxRegionEvent{
                .action = BoxRegionAction::failed,
            });
            continue;
        }
        owners.push_back(BoxOwner{*owner, record.entity});
    }
    std::sort(
        owners.begin(),
        owners.end(),
        [](const BoxOwner& left, const BoxOwner& right) {
            return box_persistent_less(left.owner, right.owner);
        }
    );

    for (const auto& entry : owners) {
        const auto spatial = world.spatial_state(entry.entity);
        auto* record = find_record(entry.entity);
        if (!spatial.has_value() || record == nullptr) {
            remove_activation_requests(entry.owner);
            box_region_events_.push_back(BoxRegionEvent{
                .owner = entry.owner,
                .action = BoxRegionAction::failed,
            });
            continue;
        }

        const world::RegionId source_region_id = spatial->region;
        const auto source_desc = world.region(source_region_id);
        auto* source = find_region(source_region_id);
        if (!source_desc.has_value() || source == nullptr ||
            !coordinator.registered(source->domain->id())) {
            remove_activation_requests(entry.owner);
            box_region_events_.push_back(BoxRegionEvent{
                .owner = entry.owner,
                .source_region = source_region_id,
                .action = BoxRegionAction::failed,
            });
            continue;
        }

        const auto participant = bindings.find(
            world,
            coordinator,
            entry.entity,
            source->domain->id()
        );
        if (!participant.has_value() || *participant != record->participant) {
            remove_activation_requests(entry.owner);
            box_region_events_.push_back(BoxRegionEvent{
                .owner = entry.owner,
                .source_region = source_region_id,
                .action = BoxRegionAction::failed,
            });
            continue;
        }

        const auto source_state = source->domain->body_state(*participant);
        if (!source_state.has_value()) {
            remove_activation_requests(entry.owner);
            box_region_events_.push_back(BoxRegionEvent{
                .owner = entry.owner,
                .source_region = source_region_id,
                .action = BoxRegionAction::failed,
            });
            continue;
        }

        const auto live_global = world::to_global_position(
            box_local_position(source_state->position),
            source_desc->origin
        );
        if (!live_global.has_value() || !world.set_spatial_state(entry.entity, world::SpatialState{
            .global_position = *live_global,
            .region = source_region_id,
        })) {
            remove_activation_requests(entry.owner);
            box_region_events_.push_back(BoxRegionEvent{
                .owner = entry.owner,
                .source_region = source_region_id,
                .action = BoxRegionAction::failed,
            });
            continue;
        }

        const bool inside_source = !source_desc->bounds.has_value() ||
            world::contains(*source_desc, *live_global);
        if (inside_source) {
            if (source_desc->bounds.has_value() && config.prefetch_seconds > 0.0F) {
                const auto predicted = predict_box_position(
                    *live_global,
                    source_state->linear_velocity,
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
                                request_region_activation(entry.owner, source_region_id, *target_desc);
                                box_region_events_.push_back(BoxRegionEvent{
                                    .owner = entry.owner,
                                    .source_region = source_region_id,
                                    .target_region = predicted_target,
                                    .action = BoxRegionAction::activation_requested,
                                });
                                continue;
                            }
                        }
                    }
                }
            }

            remove_activation_requests(entry.owner);
            box_region_events_.push_back(BoxRegionEvent{
                .owner = entry.owner,
                .source_region = source_region_id,
                .action = BoxRegionAction::stayed,
            });
            continue;
        }

        const auto target_region = world.region_containing(*live_global);
        if (!target_region.has_value() || *target_region == source_region_id) {
            remove_activation_requests(entry.owner);
            box_region_events_.push_back(BoxRegionEvent{
                .owner = entry.owner,
                .source_region = source_region_id,
                .action = BoxRegionAction::no_target,
            });
            continue;
        }

        const auto target_desc = world.region(*target_region);
        if (!target_desc.has_value()) {
            remove_activation_requests(entry.owner);
            box_region_events_.push_back(BoxRegionEvent{
                .owner = entry.owner,
                .source_region = source_region_id,
                .target_region = target_region,
                .action = BoxRegionAction::failed,
            });
            continue;
        }

        auto* target = find_region(*target_region);
        const bool target_ready = target_desc->fidelity != world::RegionFidelity::inactive &&
            target != nullptr && coordinator.registered(target->domain->id());
        if (!target_ready) {
            request_region_activation(entry.owner, source_region_id, *target_desc);
            box_region_events_.push_back(BoxRegionEvent{
                .owner = entry.owner,
                .source_region = source_region_id,
                .target_region = target_region,
                .action = BoxRegionAction::activation_requested,
            });
            continue;
        }

        if (handoff_box(world, coordinator, bindings, entry.entity, *target_region)) {
            remove_activation_requests(entry.owner);
            box_region_events_.push_back(BoxRegionEvent{
                .owner = entry.owner,
                .source_region = source_region_id,
                .target_region = target_region,
                .action = BoxRegionAction::handed_off,
            });
            continue;
        }

        remove_activation_requests(entry.owner);
        box_region_events_.push_back(BoxRegionEvent{
            .owner = entry.owner,
            .source_region = source_region_id,
            .target_region = target_region,
            .action = BoxRegionAction::failed,
        });
    }
}

} // namespace vice::rigid_body
