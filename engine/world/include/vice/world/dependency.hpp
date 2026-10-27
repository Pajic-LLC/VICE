#pragma once
#include <vice/world/persistent_id.hpp>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>
namespace vice::world {
inline constexpr std::size_t max_dependency_depth = 256;
// Nil provider denotes a source. Nil gate denotes an ungated connection.
struct Dependency {
    PersistentId provider{};
    PersistentId gate{};
    bool enabled = true;
    friend bool operator==(const Dependency&, const Dependency&) = default;
};
enum class DependencyReason : std::uint32_t { available, disabled, link_broken, upstream_unavailable };
[[nodiscard]] constexpr const char* dependency_reason_name(DependencyReason reason) noexcept {
    switch (reason) {
    case DependencyReason::available: return "available";
    case DependencyReason::disabled: return "disabled";
    case DependencyReason::link_broken: return "link_broken";
    case DependencyReason::upstream_unavailable: return "upstream_unavailable";
    }
    return "invalid";
}
struct DependencyState {
    Dependency dependency;
    bool available = true;
    DependencyReason reason = DependencyReason::available;
    DependencyReason root_reason = DependencyReason::available;
    PersistentId root_cause{};
    float break_impulse = 0;
    std::uint64_t break_step = 0;
    std::vector<PersistentId> path;
};
// Shared diagnostic representation for World inspect and Runtime entity debug.
[[nodiscard]] std::string serialize_dependency_state(const std::optional<DependencyState>& state);
}
