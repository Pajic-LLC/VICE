#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>
#include <vice/world/event.hpp>
namespace vice::world {
inline constexpr std::size_t world_schedule_capacity = 16384;
inline constexpr std::size_t world_schedule_dispatch_budget = 256;
inline constexpr std::size_t world_schedule_query_limit = 64;
enum class ScheduleCatchUp : std::uint32_t { replay, coalesce };
enum class ScheduleFailure : std::uint32_t {
    none,
    event_rejected,
    time_overflow,
    occurrence_overflow
};
struct WorldSchedule {
    PersistentId id{};
    std::uint64_t due_ticks = 0, interval_ticks = 0, occurrence = 0;
    ScheduleCatchUp catch_up = ScheduleCatchUp::coalesce;
    ScheduleFailure failure = ScheduleFailure::none;
    WorldEventResult event_result = WorldEventResult::success;
    WorldEventRequest event;
};
enum class ScheduleResult : std::uint32_t {
    success,
    invalid_argument,
    not_found,
    duplicate_id,
    capacity_exceeded,
    event_rejected
};
struct ScheduleCreation {
    ScheduleResult result = ScheduleResult::invalid_argument;
    WorldEventResult event_result = WorldEventResult::success;
    PersistentId id{};
    explicit operator bool() const noexcept { return result == ScheduleResult::success; }
};
struct SchedulerInfo {
    std::size_t count = 0, pending = 0, failed = 0;
    std::optional<std::uint64_t> next_due;
    bool due_remaining = false;
};
struct ScheduleDispatch {
    std::size_t processed = 0, fired = 0, failed = 0;
    bool due_remaining = false;
};
struct SchedulePage {
    std::vector<WorldSchedule> entries;
    bool has_more = false;
};
struct ScheduleContext {
    SchedulerInfo info;
    std::vector<WorldSchedule> entries;
    bool truncated = false;
};
bool read_world_schedule(std::istream &, WorldSchedule &);
void write_world_schedule(std::ostream &, const WorldSchedule &);
std::string serialize_scheduler_info(const SchedulerInfo &);
std::string serialize_world_schedule(const WorldSchedule &);
std::string serialize_schedule_context(const ScheduleContext &);
} // namespace vice::world
