#pragma once

#include <vice/core/log.hpp>
#include <vice/world/entity.hpp>
#include <vice/world/persistent_id.hpp>
#include <vice/world/snapshot.hpp>
#include <vice/world/spatial.hpp>
#include <vice/world/world.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
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

struct HierarchyEntry {
    world::PersistentId persistent_id{};
    std::optional<world::SpatialState> spatial_state;
};

[[nodiscard]] std::vector<HierarchyEntry> build_hierarchy(const world::World& world);

struct InspectorData {
    world::PersistentId persistent_id{};
    std::optional<world::EntityId> entity;
    std::optional<world::SpatialState> spatial_state;
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
