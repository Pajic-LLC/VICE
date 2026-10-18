#pragma once

#include <vice/core/log.hpp>
#include <vice/world/camera.hpp>
#include <vice/world/entity.hpp>
#include <vice/world/light.hpp>
#include <vice/world/persistent_id.hpp>
#include <vice/world/snapshot.hpp>
#include <vice/world/spatial.hpp>
#include <vice/world/world.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace vice::editor {

class SelectionModel {
public:
    void select(world::PersistentId persistent_id) noexcept;
    void clear() noexcept;

    [[nodiscard]] std::optional<world::PersistentId> selected() const noexcept;
    [[nodiscard]] std::optional<world::EntityId> resolve(const world::World& world) const noexcept;

private:
    std::optional<world::PersistentId> selected_;
};

enum class TransformTool : std::uint8_t {
    move,
    rotate,
    scale,
};

struct EntityTransformState {
    world::SpatialState spatial_state{};
    std::optional<world::AuthoredTransform> authored_transform;

    friend bool operator==(const EntityTransformState&, const EntityTransformState&) = default;
};

[[nodiscard]] std::optional<EntityTransformState> capture_entity_transform(
    const world::World& world,
    world::PersistentId persistent_id
);

bool apply_entity_transform(
    world::World& world,
    world::PersistentId persistent_id,
    const EntityTransformState& state
);

struct EntityCameraState {
    world::PersistentId persistent_id{};
    std::optional<world::Camera> camera;

    friend bool operator==(const EntityCameraState&, const EntityCameraState&) = default;
};

[[nodiscard]] std::optional<EntityCameraState> capture_entity_camera(
    const world::World& world,
    world::PersistentId persistent_id
);

bool apply_camera_states(
    world::World& world,
    std::span<const EntityCameraState> states
);

struct EntityLightState {
    world::PersistentId persistent_id{};
    std::optional<world::Light> light;

    friend bool operator==(const EntityLightState&, const EntityLightState&) = default;
};

[[nodiscard]] std::optional<EntityLightState> capture_entity_light(
    const world::World& world,
    world::PersistentId persistent_id
);

bool apply_entity_light(
    world::World& world,
    const EntityLightState& state
);

class CommandHistory {
public:
    struct TransformEntry {
        world::PersistentId persistent_id{};
        EntityTransformState before{};
        EntityTransformState after{};
        std::string label;
    };

    struct CameraEntry {
        std::vector<EntityCameraState> before;
        std::vector<EntityCameraState> after;
        std::string label;
    };

    struct LightEntry {
        EntityLightState before{};
        EntityLightState after{};
        std::string label;
    };

    struct CreateEntityEntry {
        world::SnapshotEntity entity;
        std::vector<world::ResourceId> owned_resources;
        std::string label;
    };

    using Entry = std::variant<TransformEntry, CameraEntry, LightEntry, CreateEntityEntry>;

    bool record(TransformEntry entry);
    bool record(CameraEntry entry);
    bool record(LightEntry entry);
    bool record(CreateEntityEntry entry);
    bool undo(world::World& world);
    bool redo(world::World& world);
    void clear() noexcept;
    void mark_saved() noexcept;

    [[nodiscard]] bool can_undo() const noexcept;
    [[nodiscard]] bool can_redo() const noexcept;
    [[nodiscard]] bool dirty() const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] std::size_t applied_count() const noexcept;
    [[nodiscard]] std::string undo_label() const;
    [[nodiscard]] std::string redo_label() const;

private:
    bool record_entry(Entry entry);

    std::vector<Entry> entries_;
    std::size_t cursor_ = 0;
    std::optional<std::size_t> saved_cursor_ = 0U;
};

class TransformHistory final : public CommandHistory {
public:
    using Entry = CommandHistory::TransformEntry;
};

[[nodiscard]] std::optional<world::PersistentId> create_mesh_instance(
    world::World& world,
    CommandHistory& history,
    world::ResourceId mesh_resource,
    world::GlobalPosition position,
    std::string* diagnostics = nullptr
);

struct HierarchyEntry {
    world::PersistentId persistent_id{};
    std::optional<world::SpatialState> spatial_state;
    std::optional<world::Camera> camera;
    std::optional<world::Light> light;
};

[[nodiscard]] std::vector<HierarchyEntry> build_hierarchy(const world::World& world);

struct InspectorData {
    world::PersistentId persistent_id{};
    std::optional<world::EntityId> entity;
    std::optional<world::SpatialState> spatial_state;
    std::optional<world::AuthoredTransform> authored_transform;
    std::optional<world::Camera> camera;
    std::optional<world::Light> light;
    std::vector<world::ResourceReference> resource_references;
    std::optional<world::RegionDesc> region;
    std::optional<world::SimulationIslandId> simulation_island;
    std::optional<world::StreamingCellId> streaming_cell;
    std::vector<world::SnapshotRelation> relations;
};

[[nodiscard]] std::optional<InspectorData> project_inspector(
    const world::World& world,
    world::PersistentId persistent_id
);

struct ConsoleEntry {
    std::uint64_t sequence = 0;
    core::LogLevel level = core::LogLevel::info;
    std::string message;
    std::string source_file;
    std::uint32_t source_line = 0;
};

class ConsoleHistory {
public:
    explicit ConsoleHistory(std::size_t capacity = 1000);
    ~ConsoleHistory();

    ConsoleHistory(const ConsoleHistory&) = delete;
    ConsoleHistory& operator=(const ConsoleHistory&) = delete;
    ConsoleHistory(ConsoleHistory&&) = delete;
    ConsoleHistory& operator=(ConsoleHistory&&) = delete;

    [[nodiscard]] bool attach();
    void detach();
    [[nodiscard]] bool attached() const noexcept;

    void push(const core::LogRecord& record);
    void clear();

    [[nodiscard]] std::vector<ConsoleEntry> entries(
        core::LogLevel minimum = core::LogLevel::trace
    ) const;
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] std::size_t capacity() const noexcept;

private:
    struct State;
    std::shared_ptr<State> state_;
    core::LogObserverId observer_id_ = core::invalid_log_observer_id;
};

} // namespace vice::editor
