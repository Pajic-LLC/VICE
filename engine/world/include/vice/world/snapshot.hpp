#pragma once

#include <vice/world/camera.hpp>
#include <vice/world/component.hpp>
#include <vice/world/light.hpp>
#include <vice/world/persistent_id.hpp>
#include <vice/world/reflection_probe.hpp>
#include <vice/world/resource.hpp>
#include <vice/world/spatial.hpp>

#include <optional>
#include <vector>

namespace vice::world {

struct SnapshotEntity {
    PersistentId persistent_id{};
    std::optional<SpatialState> spatial_state;
    std::optional<AuthoredTransform> authored_transform;
    std::optional<Camera> camera;
    std::optional<Light> light;
    std::optional<ReflectionProbe> reflection_probe;
    std::vector<ResourceReference> resource_references;
};

struct SnapshotRelation {
    PersistentId subject{};
    PersistentId kind{};
    PersistentId object{};

    friend bool operator==(const SnapshotRelation&, const SnapshotRelation&) = default;
};

struct WorldSnapshot {
    std::vector<ComponentTypeDesc> component_types;
    std::vector<ResourceId> resources;
    std::vector<RegionDesc> regions;
    std::vector<SnapshotEntity> entities;
    std::vector<SnapshotRelation> relations;
};

} // namespace vice::world
