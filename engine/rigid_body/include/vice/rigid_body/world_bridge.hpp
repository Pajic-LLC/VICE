#pragma once

#include <vice/rigid_body/rigid_body.hpp>
#include <vice/simulation/entity_bindings.hpp>
#include <vice/world/world.hpp>

#include <cstddef>
#include <optional>
#include <vector>

namespace vice::rigid_body {

class WorldBridge {
public:
    bool register_region(
        const world::World& world,
        const simulation::Coordinator& coordinator,
        world::RegionId region,
        Domain& domain
    );

    bool unregister_region(world::RegionId region) noexcept;

    bool create_box(
        world::World& world,
        const simulation::Coordinator& coordinator,
        simulation::EntityBindings& bindings,
        world::EntityId entity,
        BoxBodyDesc desc
    );

    bool sync_body_to_world(
        world::World& world,
        const simulation::Coordinator& coordinator,
        const simulation::EntityBindings& bindings,
        world::EntityId entity
    ) const;

    bool handoff_box(
        world::World& world,
        const simulation::Coordinator& coordinator,
        simulation::EntityBindings& bindings,
        world::EntityId entity,
        world::RegionId target_region
    );

    bool create_character(
        world::World& world,
        const simulation::Coordinator& coordinator,
        world::EntityId entity,
        CharacterDesc desc
    );

    [[nodiscard]] std::optional<CharacterId> character_id(
        const world::World& world,
        const simulation::Coordinator& coordinator,
        world::EntityId entity
    ) const noexcept;

    bool sync_character_to_world(
        world::World& world,
        const simulation::Coordinator& coordinator,
        world::EntityId entity
    ) const;

    bool handoff_character(
        world::World& world,
        const simulation::Coordinator& coordinator,
        world::EntityId entity,
        world::RegionId target_region
    );

    [[nodiscard]] std::size_t registered_region_count() const noexcept {
        return regions_.size();
    }

    [[nodiscard]] std::size_t character_count() const noexcept {
        return characters_.size();
    }

private:
    struct RegionDomain {
        world::RegionId region{};
        world::SimulationIslandId island{};
        Domain* domain = nullptr;
    };

    struct BoxRecord {
        world::EntityId entity{};
        simulation::ParticipantId participant{};
        BoxBodyDesc creation{};
    };

    struct CharacterRecord {
        world::PersistentId owner{};
        CharacterId character{};
        CharacterDesc creation{};
    };

    [[nodiscard]] RegionDomain* find_region(world::RegionId region) noexcept;
    [[nodiscard]] const RegionDomain* find_region(world::RegionId region) const noexcept;
    [[nodiscard]] BoxRecord* find_record(world::EntityId entity) noexcept;
    [[nodiscard]] const BoxRecord* find_record(world::EntityId entity) const noexcept;
    [[nodiscard]] CharacterRecord* find_character_record(world::PersistentId owner) noexcept;
    [[nodiscard]] const CharacterRecord* find_character_record(
        world::PersistentId owner
    ) const noexcept;
    [[nodiscard]] static std::optional<CharacterId> available_character_id(
        const Domain& domain,
        CharacterId preferred
    ) noexcept;

    std::vector<RegionDomain> regions_;
    std::vector<BoxRecord> boxes_;
    std::vector<CharacterRecord> characters_;
};

} // namespace vice::rigid_body
