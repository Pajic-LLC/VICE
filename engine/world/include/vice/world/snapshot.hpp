#pragma once

#include <vice/world/camera.hpp>
#include <vice/world/causal.hpp>
#include <vice/world/character.hpp>
#include <vice/world/clock.hpp>
#include <vice/world/component.hpp>
#include <vice/world/dependency.hpp>
#include <vice/world/distance_link.hpp>
#include <vice/world/light.hpp>
#include <vice/world/material.hpp>
#include <vice/world/persistent_id.hpp>
#include <vice/world/powered_hinge.hpp>
#include <vice/world/presentation.hpp>
#include <vice/world/quantity.hpp>
#include <vice/world/reflection_probe.hpp>
#include <vice/world/resource.hpp>
#include <vice/world/rigid_box.hpp>
#include <vice/world/rule.hpp>
#include <vice/world/schedule.hpp>
#include <vice/world/spatial.hpp>
#include <vice/world/vehicle.hpp>

#include <optional>
#include <string>
#include <vector>

namespace vice::world {

struct SnapshotEntity {
    PersistentId persistent_id{};
    std::optional<SpatialState> spatial_state;
    std::optional<AuthoredTransform> authored_transform;
    std::optional<Camera> camera;
    std::optional<Light> light;
    std::optional<ReflectionProbe> reflection_probe;
    std::optional<RigidBox> rigid_box;
    std::optional<DistanceLink> distance_link;
    std::optional<PoweredHinge> powered_hinge;
    std::optional<Dependency> dependency;
    std::optional<Character> character;
    std::optional<Vehicle> vehicle;
    std::optional<CharacterAttachment> character_attachment;
    std::optional<AnimationPlayer> animation;
    std::optional<RigPart> rig_part;
    std::optional<AudioEmitter> audio_emitter;
    PersistentId material{};
    std::vector<ResourceReference> resource_references;
    std::vector<ComponentPayload> components;
};

struct SnapshotRelation {
    PersistentId subject{};
    PersistentId kind{};
    PersistentId object{};

    std::vector<ComponentPayload> components;

    friend bool operator==(const SnapshotRelation&, const SnapshotRelation&) = default;
};

// render also omits the generic relation graph; all projections are non-persistent.
enum class ComponentPayloadSnapshot { include, omit, render };

struct WorldSnapshot {
    // Read projections are intentionally incomplete and must never be restored or saved.
    bool component_payloads_included=true;
    std::vector<ComponentTypeDesc> component_types;
    std::vector<ResourceId> resources;
    std::vector<RegionDesc> regions;
    std::vector<SnapshotEntity> entities;
    std::vector<SnapshotRelation> relations;
    CausalHistory causal_history;
    WorldClock clock;
    std::vector<WorldSchedule> schedules;
    std::vector<WorldRule> rules;
    std::vector<QuantityTypeDesc> quantity_types;
    std::vector<QuantityAccount> quantity_accounts;
    std::vector<MaterialDefinition> materials;
};

[[nodiscard]] bool validate_character_attachments(const WorldSnapshot& snapshot, std::string* diagnostics = nullptr);

[[nodiscard]] bool validate_presentation(const WorldSnapshot& snapshot,std::string* diagnostics=nullptr);

[[nodiscard]] bool validate_vehicles(const WorldSnapshot& snapshot, std::string* diagnostics = nullptr);

[[nodiscard]] bool validate_characters(const WorldSnapshot& snapshot, std::string* diagnostics = nullptr);

[[nodiscard]] bool validate_dependencies(const WorldSnapshot& snapshot, std::string* diagnostics = nullptr);

[[nodiscard]] bool validate_powered_hinges(const WorldSnapshot &snapshot,
                                           std::string *diagnostics = nullptr);

[[nodiscard]] bool validate_distance_links(const WorldSnapshot& snapshot, std::string* diagnostics = nullptr);

} // namespace vice::world
