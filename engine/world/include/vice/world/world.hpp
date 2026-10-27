#pragma once

#include <vice/world/camera.hpp>
#include <vice/world/component.hpp>
#include <vice/world/entity.hpp>
#include <vice/world/light.hpp>
#include <vice/world/persistent_id.hpp>
#include <vice/world/reflection_probe.hpp>
#include <vice/world/relation.hpp>
#include <vice/world/resource.hpp>
#include <vice/world/snapshot.hpp>
#include <vice/world/spatial.hpp>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

namespace vice::world {

class World {
public:
    [[nodiscard]] EntityId create_entity();
    [[nodiscard]] std::optional<EntityId> create_entity(PersistentId persistent_id);

    bool destroy_entity(EntityId entity);

    [[nodiscard]] bool alive(EntityId entity) const noexcept;
    [[nodiscard]] std::size_t entity_count() const noexcept;

    [[nodiscard]] std::optional<PersistentId> persistent_id(EntityId entity) const noexcept;
    [[nodiscard]] std::optional<EntityId> find(PersistentId persistent_id) const noexcept;

    bool set_material(const MaterialDefinition& material);
    bool remove_material(PersistentId material);
    [[nodiscard]] std::optional<MaterialDefinition> material(PersistentId id) const;
    bool set_entity_material(EntityId entity,PersistentId material);
    [[nodiscard]] PersistentId entity_material(EntityId entity) const noexcept;
    [[nodiscard]] std::optional<MaterialSurface> material_surface(EntityId entity) const noexcept;
    // Opaque process-local generation. Compare for equality; do not persist.
    [[nodiscard]] std::uint64_t material_revision() const noexcept { return material_revision_; }

    bool register_component_type(ComponentTypeDesc type);
    bool unregister_component_type(ComponentTypeId id);
    [[nodiscard]] std::optional<ComponentTypeDesc> component_type(ComponentTypeId id) const;
    [[nodiscard]] std::size_t component_type_count() const noexcept;

    [[nodiscard]] ResourceHandle create_resource();
    [[nodiscard]] std::optional<ResourceHandle> create_resource(ResourceId resource_id);
    bool destroy_resource(ResourceHandle resource);
    [[nodiscard]] bool resource_alive(ResourceHandle resource) const noexcept;
    [[nodiscard]] std::size_t resource_count() const noexcept;
    [[nodiscard]] std::optional<ResourceId> resource_id(ResourceHandle resource) const noexcept;
    [[nodiscard]] std::optional<ResourceHandle> find_resource(ResourceId resource_id) const noexcept;

    bool set_resource_reference(EntityId entity, ResourceReference reference);
    bool clear_resource_reference(EntityId entity, ResourceRole role);
    [[nodiscard]] std::optional<ResourceId> resource_reference(EntityId entity, ResourceRole role) const noexcept;
    [[nodiscard]] std::span<const ResourceReference> resource_references(EntityId entity) const noexcept;

    bool add_relation(Relation relation);
    bool remove_relation(const Relation& relation);
    [[nodiscard]] std::span<const Relation> relations() const noexcept;

    bool register_region(RegionDesc region);
    bool remove_region(RegionId region);
    bool set_region_fidelity(RegionId region, RegionFidelity fidelity);
    [[nodiscard]] std::optional<RegionDesc> region(RegionId region) const noexcept;
    [[nodiscard]] std::optional<RegionId> region_containing(GlobalPosition position) const noexcept;

    bool set_spatial_state(EntityId entity, SpatialState state);
    bool clear_spatial_state(EntityId entity);
    bool set_local_position(EntityId entity, LocalPosition position);
    bool handoff_spatial(EntityId entity, RegionId target_region);

    bool set_authored_transform(EntityId entity, AuthoredTransform transform);
    bool clear_authored_transform(EntityId entity);

    bool set_camera(EntityId entity, Camera camera);
    bool clear_camera(EntityId entity);
    [[nodiscard]] std::optional<Camera> camera(EntityId entity) const noexcept;
    [[nodiscard]] std::optional<EntityId> primary_camera() const noexcept;

    bool set_light(EntityId entity, Light light);
    bool clear_light(EntityId entity);
    [[nodiscard]] std::optional<Light> light(EntityId entity) const noexcept;

    bool set_reflection_probe(EntityId entity, ReflectionProbe probe);
    bool clear_reflection_probe(EntityId entity);
    [[nodiscard]] std::optional<ReflectionProbe> reflection_probe(EntityId entity) const noexcept;

    bool set_rigid_box(EntityId entity, const RigidBox& box);
    bool clear_rigid_box(EntityId entity);
    [[nodiscard]] std::optional<RigidBox> rigid_box(EntityId entity) const noexcept;
    bool set_distance_link(EntityId entity, const DistanceLink& link);
    bool clear_distance_link(EntityId entity);
    [[nodiscard]] std::optional<DistanceLink> distance_link(EntityId entity) const noexcept;
    bool set_character(EntityId entity, const Character& character);
    bool clear_character(EntityId entity);
    [[nodiscard]] std::optional<Character> character(EntityId entity) const noexcept;
    [[nodiscard]] std::uint64_t character_revision() const noexcept { return character_revision_; }
    bool begin_causal_recording(bool new_segment = false);
    [[nodiscard]] CausalInfo causal_info() const noexcept;
    [[nodiscard]] CausalHistory causal_snapshot() const;
    [[nodiscard]] std::optional<CausalEvent> causal_event(std::uint64_t sequence) const noexcept;
    [[nodiscard]] CausalContext causal_context(PersistentId subject = {}, std::size_t latest = 8, std::size_t budget = causal_query_max_events) const;
    bool publish_distance_link_break(EntityId entity,float impulse,std::uint64_t solver_step);
    bool set_dependency_enabled(EntityId entity,bool enabled);
    bool set_dependency(EntityId entity, const Dependency& dependency);
    bool clear_dependency(EntityId entity);
    [[nodiscard]] std::optional<Dependency> dependency(EntityId entity) const noexcept;
    [[nodiscard]] std::optional<DependencyState> dependency_state(EntityId entity) const;
    // No component means no supply requirement. Allocation-free, bounded render predicate.
    [[nodiscard]] bool dependency_available(EntityId entity) const noexcept;
    [[nodiscard]] std::uint64_t distance_link_revision() const noexcept { return distance_link_revision_; }

    [[nodiscard]] std::optional<SpatialState> spatial_state(EntityId entity) const noexcept;
    [[nodiscard]] std::optional<AuthoredTransform> authored_transform(EntityId entity) const noexcept;
    [[nodiscard]] std::optional<LocalPosition> local_position(EntityId entity) const noexcept;
    [[nodiscard]] std::optional<SimulationIslandId> simulation_island(EntityId entity) const noexcept;
    [[nodiscard]] std::optional<StreamingCellId> streaming_cell(EntityId entity) const noexcept;

    [[nodiscard]] WorldSnapshot snapshot() const;
    bool restore(const WorldSnapshot& snapshot);

private:
    struct Slot {
        PersistentId persistent_id;
        std::optional<SpatialState> spatial_state;
        std::optional<AuthoredTransform> authored_transform;
        std::optional<Camera> camera;
        std::optional<Light> light;
        std::optional<ReflectionProbe> reflection_probe;
        std::optional<RigidBox> rigid_box;
        std::optional<DistanceLink> distance_link;
        std::optional<Dependency> dependency;
        std::optional<Character> character;
        PersistentId material{};
        std::vector<ResourceReference> resource_references;
        std::uint32_t generation = 1;
        bool alive = false;
    };

    struct ResourceSlot {
        ResourceId resource_id;
        std::uint32_t generation = 1;
        bool alive = false;
    };

    [[nodiscard]] EntityId allocate(PersistentId persistent_id);
    [[nodiscard]] ResourceHandle allocate_resource(ResourceId resource_id);

    [[nodiscard]] bool link_endpoint_change_allowed(EntityId entity, std::optional<RegionId> region,
        std::optional<RigidMotion> motion) const;
    struct CausalKey { std::uint64_t sequence = 0; PersistentId id{}; };
    using CausalBefore = std::vector<std::pair<EntityId,std::optional<DependencyState>>>;
    [[nodiscard]] bool causal_room(std::size_t count) const noexcept;
    [[nodiscard]] CausalBefore observe_causal_dependents(EntityId entity,bool include_self) const;
    [[nodiscard]] RegionId causal_region(EntityId entity,PersistentId root = {}) const noexcept;
    CausalKey append_causal(CausalEvent event,CausalKey parent);
    void finish_causal_change(CausalBefore before,CausalEvent producer);
    bool set_distance_link_impl(EntityId entity,const DistanceLink& link,CausalKind kind);
    std::unordered_map<PersistentId,MaterialDefinition,PersistentIdHash> materials_;
    std::uint64_t material_revision_=0;
    bool advance_material_revision() noexcept;
    PersistentId causal_id_{}, capture_id_{};
    std::uint64_t next_causal_sequence_ = 1;
    std::deque<CausalEvent> causal_events_;
    bool capturing_causality_ = false;
    void install_dependency(EntityId entity, const Dependency& dependency);
    void remove_dependency_references(EntityId entity);
    std::unordered_map<PersistentId, std::vector<EntityId>, PersistentIdHash> dependency_provider_users_;
    std::unordered_map<PersistentId, std::vector<EntityId>, PersistentIdHash> dependency_gate_users_;
    std::uint64_t character_revision_ = 0;
    std::uint64_t distance_link_revision_ = 0;
    std::unordered_map<PersistentId, std::vector<EntityId>, PersistentIdHash> distance_link_users_;
    std::vector<Slot> slots_;
    std::vector<std::uint32_t> free_indices_;
    std::unordered_map<PersistentId, EntityId, PersistentIdHash> persistent_lookup_;
    std::unordered_map<ComponentTypeId, ComponentTypeDesc, ComponentTypeIdHash> component_types_;
    std::vector<ResourceSlot> resource_slots_;
    std::vector<std::uint32_t> free_resource_indices_;
    std::unordered_map<ResourceId, ResourceHandle, ResourceIdHash> resource_lookup_;
    std::vector<Relation> relations_;
    std::unordered_map<RegionId, RegionDesc, RegionIdHash> regions_;
    std::size_t entity_count_ = 0;
    std::size_t resource_count_ = 0;
};

} // namespace vice::world
