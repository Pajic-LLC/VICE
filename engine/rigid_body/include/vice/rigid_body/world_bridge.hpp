#pragma once

#include <vice/rigid_body/rigid_body.hpp>
#include <vice/simulation/entity_bindings.hpp>
#include <vice/world/world.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace vice::rigid_body {

enum class CharacterRegionAction : std::uint8_t {
    stayed,
    handed_off,
    activation_requested,
    no_target,
    failed,
};

struct CharacterRegionEvent {
    world::PersistentId owner{};
    world::RegionId source_region{};
    std::optional<world::RegionId> target_region;
    CharacterRegionAction action = CharacterRegionAction::stayed;
};

struct RegionActivationRequest {
    world::PersistentId owner{};
    world::RegionId source_region{};
    world::RegionId target_region{};
    world::SimulationIslandId target_island{};
    world::StreamingCellId target_cell{};
};

struct CharacterRegionOrchestrationConfig {
    float prefetch_seconds = 0.5F;
};

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

    void orchestrate_characters(
        world::World& world,
        const simulation::Coordinator& coordinator,
        CharacterRegionOrchestrationConfig config = {}
    );

    [[nodiscard]] std::span<const CharacterRegionEvent> character_region_events() const noexcept {
        return character_region_events_;
    }

    [[nodiscard]] std::span<const RegionActivationRequest> region_activation_requests() const noexcept {
        return region_activation_requests_;
    }

    void clear_region_activation_requests() noexcept {
        region_activation_requests_.clear();
    }

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

    void remove_activation_requests(world::PersistentId owner) noexcept;
    void request_region_activation(
        world::PersistentId owner,
        world::RegionId source_region,
        const world::RegionDesc& target
    );

    std::vector<RegionDomain> regions_;
    std::vector<BoxRecord> boxes_;
    std::vector<CharacterRecord> characters_;
    std::vector<CharacterRegionEvent> character_region_events_;
    std::vector<RegionActivationRequest> region_activation_requests_;
};

} // namespace vice::rigid_body
