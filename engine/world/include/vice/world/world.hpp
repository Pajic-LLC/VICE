#pragma once

#include <vice/world/entity.hpp>
#include <vice/world/persistent_id.hpp>
#include <vice/world/relation.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

namespace vice::world {

class World {
public:
    [[nodiscard]] EntityId create_entity();
    [[nodiscard]] std::optional<EntityId> create_entity(PersistentId persistent_id);

    bool destroy_entity(EntityId entity);

    [[nodiscard]] bool alive(EntityId entity) const noexcept;
    [[nodiscard]] std::size_t entity_count() const noexcept;

    [[nodiscard]] std::optional<PersistentId> persistent_id(EntityId entity) const noexcept;
    [[nodiscard]] std::optional<EntityId> find(PersistentId persistent_id) const noexcept;

    bool add_relation(Relation relation);
    bool remove_relation(const Relation& relation);
    [[nodiscard]] std::span<const Relation> relations() const noexcept;

private:
    struct Slot {
        PersistentId persistent_id;
        std::uint32_t generation = 1;
        bool alive = false;
    };

    [[nodiscard]] EntityId allocate(PersistentId persistent_id);

    std::vector<Slot> slots_;
    std::vector<std::uint32_t> free_indices_;
    std::unordered_map<PersistentId, EntityId, PersistentIdHash> persistent_lookup_;
    std::vector<Relation> relations_;
    std::size_t entity_count_ = 0;
};

} // namespace vice::world
