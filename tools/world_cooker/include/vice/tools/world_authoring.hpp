#pragma once

#include <vice/world/snapshot.hpp>
#include <vice/world/relation.hpp>

#include <cstddef>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace vice::tools::world_authoring {

inline constexpr std::size_t max_transaction_operations = 256U;

struct RegisterQuantityType {
    world::QuantityTypeDesc type;
};
struct SetQuantityAccount {
    world::QuantityAccount account;
};
struct RemoveQuantityAccount {
    world::QuantityKey key;
};
struct CreateSchedule {
    world::WorldSchedule schedule;
};
struct CancelSchedule {
    world::PersistentId id;
};
struct SetWorldClock {world::WorldClock clock;};

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

struct SetWorldMaterial {
    world::PersistentId id{};
    std::optional<world::MaterialDefinition> definition;
};
struct SetEntityMaterial {
    world::PersistentId entity{}, material{};
};
struct SetRigidBox {
    world::PersistentId entity{};
    std::optional<world::RigidBox> body;
};

struct SetDistanceLink {
    world::PersistentId entity{};
    std::optional<world::DistanceLink> link;
};

struct SetCharacterAttachment {world::PersistentId entity;std::optional<world::CharacterAttachment> attachment;};
struct RegisterComponentType {world::ComponentTypeDesc schema;};
struct SetComponent {world::PersistentId entity;world::ComponentPayload payload;};
struct RemoveComponent {world::PersistentId entity;world::ComponentTypeId type;};
struct SetAnimation {world::PersistentId entity;std::optional<world::AnimationPlayer> value;};
struct SetRigPart {world::PersistentId entity;std::optional<world::RigPart> value;};
struct SetAudioEmitter {world::PersistentId entity;std::optional<world::AudioEmitter> value;};
struct SetVehicle {
    world::PersistentId entity;
    std::optional<world::Vehicle> vehicle;
};
struct SetCharacter {
    world::PersistentId entity{};
    std::optional<world::Character> character;
};

struct SetDependency {
    world::PersistentId entity{};
    std::optional<world::Dependency> dependency;
};

struct SetRelationComponent {world::RelationKey relation;world::ComponentPayload payload;};
struct RemoveRelationComponent {world::RelationKey relation;world::ComponentTypeId type;};
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

using Operation =
    std::variant<RegisterQuantityType, SetQuantityAccount, RemoveQuantityAccount, SetWorldClock,
                 CreateSchedule, CancelSchedule, SetEntitySpatial, SetEntityTransform,
                 SetRegionFidelity, SetRigidBox, SetWorldMaterial, SetEntityMaterial,
                 SetDistanceLink, SetDependency, SetCharacter, SetVehicle, RegisterComponentType,
                 SetComponent, RemoveComponent, SetAnimation, SetRigPart, SetAudioEmitter,
                 SetCharacterAttachment, SetRelationComponent, RemoveRelationComponent, AddRelation,
                 RemoveRelation, AddResourceReference, RemoveResourceReference>;

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
    std::optional<std::size_t> failed_operation;

    [[nodiscard]] explicit operator bool() const noexcept { return success; }
};

[[nodiscard]] Result apply(
    const world::WorldSnapshot& authoritative,
    const Transaction& transaction
);

} // namespace vice::tools::world_authoring
