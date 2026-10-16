#pragma once

#include <vice/world/persistent_id.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>

namespace vice::world {

struct GlobalPosition {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;

    friend bool operator==(const GlobalPosition&, const GlobalPosition&) = default;
};

struct LocalPosition {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;

    friend bool operator==(const LocalPosition&, const LocalPosition&) = default;
};

struct RegionId {
    std::uint64_t value = 0;

    [[nodiscard]] constexpr bool valid() const noexcept {
        return value != 0;
    }

    friend bool operator==(const RegionId&, const RegionId&) = default;
};

struct RegionIdHash {
    [[nodiscard]] std::size_t operator()(const RegionId id) const noexcept {
        return std::hash<std::uint64_t>{}(id.value);
    }
};

struct SimulationIslandId {
    std::uint64_t value = 0;

    [[nodiscard]] constexpr bool valid() const noexcept {
        return value != 0;
    }

    friend bool operator==(const SimulationIslandId&, const SimulationIslandId&) = default;
};

struct StreamingCellId {
    std::int64_t x = 0;
    std::int64_t y = 0;
    std::int64_t z = 0;

    friend bool operator==(const StreamingCellId&, const StreamingCellId&) = default;
};

enum class RegionFidelity : std::uint8_t {
    inactive,
    reduced,
    full,
};

struct RegionDesc {
    RegionId id{};
    SimulationIslandId simulation_island{};
    GlobalPosition origin{};
    StreamingCellId streaming_cell{};
    RegionFidelity fidelity = RegionFidelity::full;
};

struct SpatialState {
    GlobalPosition global_position{};
    RegionId region{};
};

struct SpatialReference {
    PersistentId entity{};
    std::optional<StreamingCellId> last_known_cell{};
};

[[nodiscard]] inline bool finite(const GlobalPosition value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

[[nodiscard]] inline bool finite(const LocalPosition value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

[[nodiscard]] inline std::optional<LocalPosition> to_local_position(
    const GlobalPosition value,
    const GlobalPosition origin
) noexcept {
    if (!finite(value) || !finite(origin)) {
        return std::nullopt;
    }

    const double x = value.x - origin.x;
    const double y = value.y - origin.y;
    const double z = value.z - origin.z;
    constexpr double max_float = static_cast<double>(std::numeric_limits<float>::max());
    if (std::abs(x) > max_float || std::abs(y) > max_float || std::abs(z) > max_float) {
        return std::nullopt;
    }

    const LocalPosition local{
        static_cast<float>(x),
        static_cast<float>(y),
        static_cast<float>(z),
    };
    if (!finite(local)) {
        return std::nullopt;
    }
    return local;
}

[[nodiscard]] inline std::optional<GlobalPosition> to_global_position(
    const LocalPosition value,
    const GlobalPosition origin
) noexcept {
    if (!finite(value) || !finite(origin)) {
        return std::nullopt;
    }

    const GlobalPosition global{
        origin.x + static_cast<double>(value.x),
        origin.y + static_cast<double>(value.y),
        origin.z + static_cast<double>(value.z),
    };
    if (!finite(global)) {
        return std::nullopt;
    }
    return global;
}

[[nodiscard]] inline std::optional<LocalPosition> camera_relative_position(
    const GlobalPosition value,
    const GlobalPosition camera_position
) noexcept {
    return to_local_position(value, camera_position);
}

} // namespace vice::world
