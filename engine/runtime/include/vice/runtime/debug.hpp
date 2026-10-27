#pragma once

#include <vice/world/persistent_id.hpp>
#include <vice/world/spatial.hpp>
#include <vice/world/world.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace vice::runtime {

enum class RuntimeDebugEntityState : std::uint8_t {
    available,
    not_found,
    invalid_request,
};

struct RuntimeDebugRegionSnapshot {
    world::RegionId id{};
    world::SimulationIslandId simulation_island{};
    world::GlobalPosition origin{};
    world::StreamingCellId streaming_cell{};
    world::RegionFidelity fidelity = world::RegionFidelity::inactive;
};

struct RuntimeDebugEntitySnapshot {
    RuntimeDebugEntityState state = RuntimeDebugEntityState::invalid_request;
    world::PersistentId requested_id{};
    std::optional<world::SpatialState> spatial;
    std::optional<world::AuthoredTransform> authored_transform;
    std::optional<world::RigidBox> rigid_box;
    std::optional<world::DistanceLink> distance_link;
    std::optional<RuntimeDebugRegionSnapshot> region;
    bool camera = false;
    bool light = false;
    bool reflection_probe = false;
    std::size_t resource_reference_count = 0U;
    std::size_t outgoing_relation_count = 0U;
    std::size_t incoming_relation_count = 0U;
};

[[nodiscard]] RuntimeDebugEntitySnapshot sample_runtime_debug_entity(
    const world::World& world,
    world::PersistentId persistent_id
);

[[nodiscard]] std::string serialize_runtime_debug_entity_snapshot(
    const RuntimeDebugEntitySnapshot& snapshot,
    std::string_view session_id
);

} // namespace vice::runtime
