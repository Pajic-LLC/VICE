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

struct AuthoredRotation {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
    float w = 1.0F;

    friend bool operator==(const AuthoredRotation&, const AuthoredRotation&) = default;
};

struct AuthoredScale {
    float x = 1.0F;
    float y = 1.0F;
    float z = 1.0F;

    friend bool operator==(const AuthoredScale&, const AuthoredScale&) = default;
};

struct AuthoredTransform {
    AuthoredRotation rotation{};
    AuthoredScale scale{};

    friend bool operator==(const AuthoredTransform&, const AuthoredTransform&) = default;
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

struct RegionBounds {
    LocalPosition min{};
    LocalPosition max{};

    friend bool operator==(const RegionBounds&, const RegionBounds&) = default;
};

struct RegionDesc {
    RegionId id{};
    SimulationIslandId simulation_island{};
    GlobalPosition origin{};
    StreamingCellId streaming_cell{};
    RegionFidelity fidelity = RegionFidelity::full;
    std::optional<RegionBounds> bounds;
};

struct SpatialState {
    GlobalPosition global_position{};
    RegionId region{};

    friend bool operator==(const SpatialState&, const SpatialState&) = default;
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

[[nodiscard]] inline bool finite(const AuthoredRotation value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) &&
        std::isfinite(value.z) && std::isfinite(value.w);
}

[[nodiscard]] inline bool finite(const AuthoredScale value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

[[nodiscard]] inline bool valid(const AuthoredRotation rotation) noexcept {
    if (!finite(rotation)) {
        return false;
    }
    const float length_squared =
        rotation.x * rotation.x + rotation.y * rotation.y +
        rotation.z * rotation.z + rotation.w * rotation.w;
    return std::abs(length_squared - 1.0F) <= 0.001F;
}

[[nodiscard]] inline bool valid(const AuthoredScale scale) noexcept {
    return finite(scale) && scale.x > 0.0F && scale.y > 0.0F && scale.z > 0.0F;
}

[[nodiscard]] inline bool valid(const AuthoredTransform& transform) noexcept {
    return valid(transform.rotation) && valid(transform.scale);
}

[[nodiscard]] inline bool valid(const RegionBounds& bounds) noexcept {
    return finite(bounds.min) && finite(bounds.max) &&
        bounds.min.x < bounds.max.x &&
        bounds.min.y < bounds.max.y &&
        bounds.min.z < bounds.max.z;
}

[[nodiscard]] inline bool contains(
    const RegionBounds& bounds,
    const LocalPosition position
) noexcept {
    return valid(bounds) && finite(position) &&
        position.x >= bounds.min.x && position.x < bounds.max.x &&
        position.y >= bounds.min.y && position.y < bounds.max.y &&
        position.z >= bounds.min.z && position.z < bounds.max.z;
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

[[nodiscard]] inline bool contains(
    const RegionDesc& region,
    const GlobalPosition position
) noexcept {
    if (!region.bounds.has_value()) {
        return false;
    }
    const auto local = to_local_position(position, region.origin);
    return local.has_value() && contains(*region.bounds, *local);
}

[[nodiscard]] inline std::optional<LocalPosition> camera_relative_position(
    const GlobalPosition value,
    const GlobalPosition camera_position
) noexcept {
    return to_local_position(value, camera_position);
}

} // namespace vice::world
