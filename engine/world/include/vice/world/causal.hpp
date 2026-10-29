#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>
#include <vice/world/dependency.hpp>
#include <vice/world/event.hpp>
#include <vice/world/spatial.hpp>
namespace vice::world {
inline constexpr std::size_t causal_history_capacity = 1024;
inline constexpr std::size_t causal_query_max_events = 64;
enum class CausalKind : std::uint8_t {
    capture_started,
    distance_link_changed,
    distance_link_removed,
    constraint_broken,
    dependency_configured,
    dependency_removed,
    dependency_enabled,
    dependency_state_changed,
    world_event
};
enum class CausalOrigin : std::uint8_t { runtime, authoring, physics, gameplay, derived };
enum class CausalQuantity : std::uint8_t { none, linear_impulse };
[[nodiscard]] const char* causal_kind_name(CausalKind value) noexcept;
[[nodiscard]] const char* causal_origin_name(CausalOrigin value) noexcept;
struct CausalEvent {
    PersistentId id{}, parent_id{}, capture_id{};
    std::uint64_t sequence = 0, parent_sequence = 0;
    CausalKind kind = CausalKind::capture_started;
    CausalOrigin origin = CausalOrigin::runtime;
    PersistentId subject{}, first{}, second{}, root_entity{};
    PersistentId first_material{}, second_material{};
    bool value = false;
    DependencyReason reason = DependencyReason::available, root_reason = DependencyReason::available;
    CausalQuantity quantity_kind = CausalQuantity::none;
    float quantity = 0;
    std::uint64_t solver_step = 0;
    RegionId region{};
    std::uint64_t world_ticks = 0;
    std::optional<WorldEventData> data;
    friend bool operator==(const CausalEvent&,const CausalEvent&) = default;
};
struct CausalHistory {
    PersistentId id{};
    std::uint64_t next_sequence = 1;
    std::vector<CausalEvent> events;
};
struct CausalInfo {
    PersistentId id{}, capture_id{};
    std::uint64_t first_sequence = 1, next_sequence = 1;
    std::size_t retained_count = 0;
    bool capturing = false;
};
struct SignalContext {
    bool truncated = false;
    SignalInfo info;
    std::uint64_t causal_first_sequence = 1;
    std::vector<CausalEvent> events;
};
struct CausalContext {
    CausalInfo info;
    std::vector<CausalEvent> events;
    bool truncated = false;
};
[[nodiscard]] std::string serialize_signal_context(const SignalContext &);
[[nodiscard]] bool validate_causal_history(const CausalHistory& history,std::string* diagnostics = nullptr);
[[nodiscard]] std::string serialize_causal_context(const CausalContext& context);
}
