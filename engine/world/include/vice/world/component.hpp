#pragma once

#include <vice/world/persistent_id.hpp>

#include <cstddef>
#include <cstdint>
#include <string>

namespace vice::world {

struct ComponentTypeId {
    PersistentId value{};

    [[nodiscard]] constexpr bool valid() const noexcept {
        return !value.is_nil();
    }

    friend constexpr bool operator==(const ComponentTypeId&, const ComponentTypeId&) noexcept = default;
};

struct ComponentTypeIdHash {
    [[nodiscard]] std::size_t operator()(const ComponentTypeId& id) const noexcept {
        return PersistentIdHash{}(id.value);
    }
};

struct ComponentTypeDesc {
    ComponentTypeId id{};
    std::string name;
    std::uint32_t version = 1;

    friend bool operator==(const ComponentTypeDesc&, const ComponentTypeDesc&) = default;
};

} // namespace vice::world
