#pragma once

#include <vice/world/snapshot.hpp>

#include <cstddef>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace vice::tools::world_authoring {

inline constexpr std::size_t max_transaction_operations = 256U;

struct SetEntitySpatial {
    world::PersistentId entity{};
    world::SpatialState spatial{};
};

struct SetEntityTransform {
    world::PersistentId entity{};
    std::optional<world::AuthoredTransform> transform;
};

struct SetRegionFidelity {
    world::RegionId region{};
    world::RegionFidelity fidelity = world::RegionFidelity::full;
};

struct AddRelation {
    world::SnapshotRelation relation{};
};

struct RemoveRelation {
    world::SnapshotRelation relation{};
};

struct AddResourceReference {
    world::PersistentId entity{};
    world::ResourceReference reference{};
};

struct RemoveResourceReference {
    world::PersistentId entity{};
    world::ResourceReference reference{};
};

using Operation = std::variant<
    SetEntitySpatial,
    SetEntityTransform,
    SetRegionFidelity,
    AddRelation,
    RemoveRelation,
    AddResourceReference,
    RemoveResourceReference
>;

struct Transaction {
    std::vector<Operation> operations;
};

struct Result {
    bool success = false;
    bool changed = false;
    std::size_t applied_operations = 0U;
    world::WorldSnapshot snapshot;
    std::string serialized_source;
    std::string diagnostics;

    [[nodiscard]] explicit operator bool() const noexcept { return success; }
};

[[nodiscard]] Result apply(
    const world::WorldSnapshot& authoritative,
    const Transaction& transaction
);

} // namespace vice::tools::world_authoring
