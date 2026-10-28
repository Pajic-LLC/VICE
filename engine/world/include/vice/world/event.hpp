#pragma once
#include <cstdint>
#include <optional>
#include <vector>
#include <vice/world/component.hpp>
#include <vice/world/spatial.hpp>
namespace vice::world {
inline constexpr std::size_t world_event_parent_capacity = 4;
inline constexpr std::size_t world_signal_capacity = 256;
inline constexpr std::uint64_t world_signal_lifetime_ticks = 1'000'000;
struct EventRef {
    PersistentId history{}, id{};
    std::uint64_t sequence = 0;
    friend bool operator==(const EventRef &, const EventRef &) = default;
};
struct ScheduleProvenance {
    PersistentId id{};
    std::uint64_t due_ticks = 0, occurrence = 0, count = 0;
    friend bool operator==(const ScheduleProvenance &, const ScheduleProvenance &) = default;
};
struct WorldEventData {
    ComponentPayload payload;
    std::vector<EventRef> parents;
    std::optional<ScheduleProvenance> schedule;
    friend bool operator==(const WorldEventData &, const WorldEventData &) = default;
};
struct WorldEventRequest {
    PersistentId subject{}, target{}, object{};
    RegionId region{};
    WorldEventData data;
    bool retained = true;
};
enum class WorldEventResult : std::uint32_t {
    success,
    invalid_argument,
    missing_entity,
    missing_schema,
    schema_conflict,
    history_mismatch,
    parent_pruned,
    invalid_parent,
    sequence_exhausted
};
struct WorldEventEmission {
    WorldEventResult result = WorldEventResult::invalid_argument;
    EventRef ref;
    explicit operator bool() const noexcept { return result == WorldEventResult::success; }
};
struct SignalInfo {
    PersistentId stream{};
    std::uint64_t first_sequence = 1, next_sequence = 1;
    std::size_t retained_count = 0;
};
bool read_world_event_data(std::istream &, WorldEventData &);
void write_world_event_data(std::ostream &, const WorldEventData &);
} // namespace vice::world
