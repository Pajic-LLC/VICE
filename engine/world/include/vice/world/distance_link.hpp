#pragma once
#include <vice/world/persistent_id.hpp>
#include <cmath>
#include <cstdint>
namespace vice::world {
// Logical entity component. Distances are meters; impulses are N*s. Zero threshold is unbreakable.
struct DistanceLink {
    PersistentId first{};
    PersistentId second{};
    float min_distance = 0;
    float max_distance = 1;
    float frequency_hz = 0;
    float damping_ratio = 0;
    float break_threshold = 0;
    bool broken = false;
    float last_break_impulse = 0;
    std::uint64_t break_step = 0;
    friend bool operator==(const DistanceLink&, const DistanceLink&) = default;
};
[[nodiscard]] inline bool valid(const DistanceLink& link) noexcept {
    return !link.first.is_nil() && !link.second.is_nil() && link.first != link.second &&
        std::isfinite(link.min_distance) && link.min_distance >= 0 &&
        std::isfinite(link.max_distance) && link.max_distance >= link.min_distance &&
        std::isfinite(link.frequency_hz) && link.frequency_hz >= 0 &&
        std::isfinite(link.damping_ratio) && link.damping_ratio >= 0 &&
        std::isfinite(link.break_threshold) && link.break_threshold >= 0 &&
        std::isfinite(link.last_break_impulse) && link.last_break_impulse >= 0 &&
        (link.broken || (link.last_break_impulse == 0 && link.break_step == 0));
}
}
