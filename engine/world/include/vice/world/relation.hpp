#pragma once

#include <vice/world/entity.hpp>
#include <vice/world/persistent_id.hpp>

namespace vice::world {

struct Relation {
    EntityId subject;
    PersistentId kind;
    EntityId object;

    friend bool operator==(const Relation&, const Relation&) noexcept = default;
};

} // namespace vice::world
