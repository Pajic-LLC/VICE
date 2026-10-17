#pragma once

#include <vice/world/persistent_id.hpp>

#include <cstddef>
#include <cstdint>
#include <limits>

namespace vice::world {

struct ResourceId {
    PersistentId value{};

    [[nodiscard]] constexpr bool valid() const noexcept {
        return !value.is_nil();
    }

    friend constexpr bool operator==(const ResourceId&, const ResourceId&) noexcept = default;
};

struct ResourceIdHash {
    [[nodiscard]] std::size_t operator()(const ResourceId& id) const noexcept {
        return PersistentIdHash{}(id.value);
    }
};

struct ResourceHandle {
    static constexpr std::uint32_t invalid_index = std::numeric_limits<std::uint32_t>::max();

    std::uint32_t index = invalid_index;
    std::uint32_t generation = 0;

    [[nodiscard]] constexpr bool valid() const noexcept {
        return index != invalid_index && generation != 0;
    }

    friend constexpr bool operator==(const ResourceHandle&, const ResourceHandle&) noexcept = default;
};

enum class ResourceRole : std::uint8_t {
    render_mesh = 1,
};

[[nodiscard]] constexpr bool valid(const ResourceRole role) noexcept {
    return role == ResourceRole::render_mesh;
}

struct ResourceReference {
    ResourceRole role = ResourceRole::render_mesh;
    ResourceId resource{};

    friend constexpr bool operator==(const ResourceReference&, const ResourceReference&) noexcept = default;
};

} // namespace vice::world
