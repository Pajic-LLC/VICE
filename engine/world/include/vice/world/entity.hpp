#pragma once

#include <cstdint>
#include <limits>

namespace vice::world {

struct EntityId {
    static constexpr std::uint32_t invalid_index = std::numeric_limits<std::uint32_t>::max();

    std::uint32_t index = invalid_index;
    std::uint32_t generation = 0;

    [[nodiscard]] constexpr bool valid() const noexcept {
        return index != invalid_index && generation != 0;
    }

    friend constexpr bool operator==(const EntityId&, const EntityId&) noexcept = default;
};

} // namespace vice::world
