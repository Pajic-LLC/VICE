#include <vice/editor/preview_runtime.hpp>

#include <vice/rigid_body/collision_asset.hpp>
#include <vice/rigid_body/material_asset.hpp>
#include <vice/world/persistent_id.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace vice::editor {
namespace {

constexpr std::string_view player_id_text = "10000000-0000-4000-8000-000000000001";
constexpr std::string_view cube_id_text = "10000000-0000-4000-8000-000000000002";
constexpr std::string_view ramp_id_text = "10000000-0000-4000-8000-000000000005";
constexpr std::uint64_t cube_participant_value = 20U;
constexpr std::uint64_t ramp_participant_value = 30U;
constexpr std::uint64_t terrain_participant_base = 1'000U;
constexpr std::size_t max_preview_steps_per_frame = 8U;

[[nodiscard]] simulation::Fidelity simulation_fidelity(const world::RegionFidelity fidelity) noexcept {
    switch (fidelity) {
    case world::RegionFidelity::full: return simulation::Fidelity::full;
    case world::RegionFidelity::reduced: return simulation::Fidelity::reduced;
    case world::RegionFidelity::inactive: return simulation::Fidelity::off;
    }
    return simulation::Fidelity::off;
}

[[nodiscard]] const AssetRecord* find_asset(
    const AssetIndex& assets,
    const AssetKind kind,
    const std::string_view name
) noexcept {
    const auto records = assets.assets();
    const auto found = std::find_if(
        records.begin(), records.end(),
        [&](const AssetRecord& record) {
            return record.kind == kind && record.name == name && record.cooked_valid &&
                !record.cooked_path.empty();
        }
    );
    return found != records.end() ? &*found : nullptr;
}

[[nodiscard]] simulation::MaterialId find_material_id(
    const rigid_body::RigidMaterialAsset& asset,
    const std::string_view name
) noexcept {
    const auto found = std::find_if(
        asset.materials.begin(), asset.materials.end(),
        [&](const rigid_body::RigidMaterialDefinition& material) {
            return material.name == name;
        }
    );
    return found != asset.materials.end() ? found->runtime_id : simulation::MaterialId{};
}

[[nodiscard]] world::LocalPosition local_position(const math::Vec3 value) noexcept {
    return {value.x, value.y, value.z};
}

[[nodiscard]] world::GlobalPosition global_position(
    const world::RegionDesc& region,
    const math::Vec3 local
) noexcept {
    return world::to_global_position(local_position(local), region.origin).value_or(region.origin);
}

} // namespace

struct PreviewRuntime::State {
    struct RegionRuntime {
        world::RegionId region{};
        std::unique_ptr<rigid_body::Domain> domain;
    };

    struct BoxRuntime {
        world::PersistentId owner{};
        rigid_body::BoxBodyDesc desc{};
    };

    struct CharacterRuntime {
        world::PersistentId owner{};
        rigid_body::CharacterDesc desc{};
    };

    jobs::Scheduler* jobs = nullptr;
    world::World world;
    simulation::Coordinator simulation{1.0 / 120.0};
    simulation::EntityBindings bindings;
    rigid_body::WorldBridge bridge;
    std::vector<RegionRuntime> regions;
    std::vector<BoxRuntime> boxes;
    std::vector<CharacterRuntime> characters;
    std::vector<rigid_body::CharacterRegionEvent> recent_character_events;
    std::vector<rigid_body::BoxRegionEvent> recent_box_events;
    std::vector<rigid_body::RegionActivationRequest> recent_activation_requests;
    PreviewPlayback playback = PreviewPlayback::stopped;
    double accumulator = 0.0;

    [[nodiscard]] RegionRuntime* region_runtime(const world::RegionId region) noexcept {
        const auto found = std::find_if(
            regions.begin(), regions.end(),
            [region](const RegionRuntime& entry) { return entry.region == region; }
        );
        return found != regions.end() ? &*found : nullptr;
    }

    [[nodiscard]] const RegionRuntime* region_runtime(const world::RegionId region) const noexcept {
        const auto found = std::find_if(
            regions.begin(), regions.end(),
            [region](const RegionRuntime& entry) { return entry.region == region; }
        );
        return found != regions.end() ? &*found : nullptr;
    }

    [[nodiscard]] bool fixed_step() {
        simulation.step();
        bridge.orchestrate_boxes(world, simulation, bindings);
        bridge.orchestrate_characters(world, simulation);

        recent_box_events.assign(bridge.box_region_events().begin(), bridge.box_region_events().end());
        recent_character_events.assign(
            bridge.character_region_events().begin(), bridge.character_region_events().end()
        );
        recent_activation_requests.assign(
            bridge.region_activation_requests().begin(), bridge.region_activation_requests().end()
        );

        for (const auto& request : recent_activation_requests) {
            auto* runtime = region_runtime(request.target_region);
            if (runtime == nullptr || runtime->domain == nullptr ||
                !bridge.activate_region(world, simulation, request.target_region, *runtime->domain)) {
                return false;
            }
        }
        bridge.clear_region_activation_requests();
        return true;
    }
};

PreviewRuntime::PreviewRuntime() : state_{std::make_unique<State>()} {}
PreviewRuntime::~PreviewRuntime() = default;
PreviewRuntime::PreviewRuntime(PreviewRuntime&&) noexcept = default;
PreviewRuntime& PreviewRuntime::operator=(PreviewRuntime&&) noexcept = default;

bool PreviewRuntime::reset(
    const world::World& authored_world,
    jobs::Scheduler& jobs,
    const AssetIndex& assets,
    std::string* diagnostics
) {
    auto next = std::make_unique<State>();
    next->jobs = &jobs;
    if (!next->world.restore(authored_world.snapshot())) {
        if (diagnostics != nullptr) *diagnostics = "Could not clone the authored World.";
        return false;
    }

    const auto snapshot = next->world.snapshot();
    next->regions.reserve(snapshot.regions.size());
    for (std::size_t index = 0; index < snapshot.regions.size(); ++index) {
        const auto& region = snapshot.regions[index];
        auto domain = rigid_body::Domain::create(
            simulation::DomainId{static_cast<std::uint64_t>(index + 1U)},
            "editor.preview.region." + std::to_string(region.id.value),
            jobs
        );
        if (!domain || !next->simulation.register_domain(*domain)) {
            if (diagnostics != nullptr) *diagnostics = "Could not create a preview simulation domain.";
            return false;
        }
        domain->set_fidelity(simulation_fidelity(region.fidelity));
        next->regions.push_back(State::RegionRuntime{.region = region.id, .domain = std::move(domain)});
        if (!next->bridge.register_region(
                next->world,
                next->simulation,
                region.id,
                *next->regions.back().domain
            )) {
            if (diagnostics != nullptr) *diagnostics = "Could not bind a preview region to its domain.";
            return false;
        }
    }

    simulation::MaterialId terrain_material{};
    simulation::MaterialId cube_material{};
    if (const auto* material_record = find_asset(assets, AssetKind::rigid_material, "vice_lab");
        material_record != nullptr) {
        auto loaded = rigid_body::load_rigid_material_asset(material_record->cooked_path);
        if (loaded.asset.has_value()) {
            terrain_material = find_material_id(*loaded.asset, "terrain_concrete");
            cube_material = find_material_id(*loaded.asset, "cube_alloy");
            for (auto& region : next->regions) {
                std::string material_diagnostics;
                if (!rigid_body::register_rigid_material_asset(
                        *region.domain,
                        *loaded.asset,
                        &material_diagnostics
                    )) {
                    if (diagnostics != nullptr) {
                        *diagnostics = "Could not register preview rigid materials: " + material_diagnostics;
                    }
                    return false;
                }
            }
        }
    }

    std::optional<rigid_body::CollisionAsset> terrain_collision;
    if (const auto* terrain_record = find_asset(assets, AssetKind::collision, "vice_lab_terrain");
        terrain_record != nullptr) {
        auto loaded = rigid_body::load_collision_asset(terrain_record->cooked_path);
        if (loaded.asset.has_value() && !loaded.asset->shapes.empty()) {
            terrain_collision = std::move(*loaded.asset);
            for (auto& region : next->regions) {
                std::string collision_diagnostics;
                if (!rigid_body::register_collision_asset(
                        *region.domain,
                        *terrain_collision,
                        &collision_diagnostics
                    )) {
                    if (diagnostics != nullptr) {
                        *diagnostics = "Could not register preview terrain collision: " + collision_diagnostics;
                    }
                    return false;
                }
            }
        }
    }

    for (std::size_t index = 0; index < next->regions.size(); ++index) {
        auto& runtime = next->regions[index];
        const auto region = next->world.region(runtime.region);
        if (!region.has_value()) continue;

        if (terrain_collision.has_value()) {
            rigid_body::BodyDesc terrain;
            terrain.participant = simulation::ParticipantId{terrain_participant_base + index};
            terrain.material = terrain_material;
            terrain.shape = terrain_collision->shapes.front().id;
            terrain.motion = rigid_body::MotionType::static_body;
            if (!runtime.domain->create_body(terrain)) {
                if (diagnostics != nullptr) *diagnostics = "Could not create preview terrain collision.";
                return false;
            }
        } else {
            rigid_body::BoxBodyDesc floor;
            floor.participant = simulation::ParticipantId{terrain_participant_base + index};
            floor.material = terrain_material;
            floor.motion = rigid_body::MotionType::static_body;
            if (region->bounds.has_value()) {
                const auto& bounds = *region->bounds;
                floor.half_extents = {
                    std::max(0.5F, (bounds.max.x - bounds.min.x) * 0.5F),
                    0.25F,
                    std::max(0.5F, (bounds.max.z - bounds.min.z) * 0.5F),
                };
                floor.position = {
                    (bounds.min.x + bounds.max.x) * 0.5F,
                    -0.25F,
                    (bounds.min.z + bounds.max.z) * 0.5F,
                };
            } else {
                floor.half_extents = {20.0F, 0.25F, 20.0F};
                floor.position = {0.0F, -0.25F, 0.0F};
            }
            if (!runtime.domain->create_box(floor)) {
                if (diagnostics != nullptr) *diagnostics = "Could not create preview fallback terrain.";
                return false;
            }
        }
    }

    const auto ramp_id = world::PersistentId::parse(ramp_id_text);
    if (ramp_id.has_value()) {
        const auto ramp_entity = next->world.find(*ramp_id);
        const auto* ramp_record = find_asset(assets, AssetKind::collision, "vice_lab_ramp");
        if (ramp_entity.has_value() && ramp_record != nullptr) {
            auto loaded = rigid_body::load_collision_asset(ramp_record->cooked_path);
            const auto spatial = next->world.spatial_state(*ramp_entity);
            if (loaded.asset.has_value() && !loaded.asset->shapes.empty() && spatial.has_value()) {
                auto* runtime = next->region_runtime(spatial->region);
                if (runtime != nullptr) {
                    std::string collision_diagnostics;
                    if (!rigid_body::register_collision_asset(
                            *runtime->domain,
                            *loaded.asset,
                            &collision_diagnostics
                        )) {
                        if (diagnostics != nullptr) {
                            *diagnostics = "Could not register preview ramp collision: " + collision_diagnostics;
                        }
                        return false;
                    }
                    rigid_body::BodyDesc ramp;
                    ramp.participant = simulation::ParticipantId{ramp_participant_value};
                    ramp.material = terrain_material;
                    ramp.shape = loaded.asset->shapes.front().id;
                    ramp.motion = rigid_body::MotionType::static_body;
                    const auto local = next->world.local_position(*ramp_entity);
                    if (local.has_value()) ramp.position = {local->x, local->y, local->z};
                    if (!runtime->domain->create_body(ramp)) {
                        if (diagnostics != nullptr) *diagnostics = "Could not create preview ramp collision.";
                        return false;
                    }
                }
            }
        }
    }

    const auto cube_id = world::PersistentId::parse(cube_id_text);
    if (cube_id.has_value()) {
        const auto cube_entity = next->world.find(*cube_id);
        if (cube_entity.has_value()) {
            rigid_body::BoxBodyDesc cube;
            cube.participant = simulation::ParticipantId{cube_participant_value};
            cube.material = cube_material;
            cube.motion = rigid_body::MotionType::dynamic_body;
            cube.motion_quality = rigid_body::MotionQuality::continuous;
            cube.half_extents = {0.7F, 0.7F, 0.7F};
            cube.linear_velocity = {2.5F, 0.0F, 0.0F};
            if (!next->bridge.create_box(
                    next->world,
                    next->simulation,
                    next->bindings,
                    *cube_entity,
                    cube
                )) {
                if (diagnostics != nullptr) *diagnostics = "Could not create the preview cube.";
                return false;
            }
            next->boxes.push_back(State::BoxRuntime{.owner = *cube_id, .desc = cube});
        }
    }

    const auto player_id = world::PersistentId::parse(player_id_text);
    if (player_id.has_value()) {
        const auto player_entity = next->world.find(*player_id);
        if (player_entity.has_value()) {
            rigid_body::CharacterDesc character;
            character.id = rigid_body::CharacterId{1};
            character.height = 1.8F;
            character.radius = 0.3F;
            character.step_height = 0.4F;
            character.stick_to_floor_distance = 0.5F;
            if (!next->bridge.create_character(next->world, next->simulation, *player_entity, character)) {
                if (diagnostics != nullptr) *diagnostics = "Could not create the preview character.";
                return false;
            }
            next->characters.push_back(State::CharacterRuntime{.owner = *player_id, .desc = character});
        }
    }

    next->playback = PreviewPlayback::paused;
    if (diagnostics != nullptr) diagnostics->clear();
    state_ = std::move(next);
    return true;
}

void PreviewRuntime::stop() noexcept {
    state_ = std::make_unique<State>();
}

void PreviewRuntime::play() noexcept {
    if (active()) state_->playback = PreviewPlayback::running;
}

void PreviewRuntime::pause() noexcept {
    if (active()) state_->playback = PreviewPlayback::paused;
}

bool PreviewRuntime::step_once() {
    return active() && state_->fixed_step();
}

bool PreviewRuntime::update(const double delta_seconds) {
    if (!active() || state_->playback != PreviewPlayback::running ||
        !std::isfinite(delta_seconds) || delta_seconds <= 0.0) {
        return true;
    }

    state_->accumulator += std::min(delta_seconds, 0.25);
    std::size_t steps = 0;
    while (state_->accumulator >= state_->simulation.fixed_delta_seconds() &&
        steps < max_preview_steps_per_frame) {
        if (!state_->fixed_step()) return false;
        state_->accumulator -= state_->simulation.fixed_delta_seconds();
        ++steps;
    }
    if (steps == max_preview_steps_per_frame) state_->accumulator = 0.0;
    return true;
}

bool PreviewRuntime::active() const noexcept {
    return state_ != nullptr && state_->playback != PreviewPlayback::stopped;
}

PreviewPlayback PreviewRuntime::playback() const noexcept {
    return state_ != nullptr ? state_->playback : PreviewPlayback::stopped;
}

const simulation::Coordinator* PreviewRuntime::coordinator() const noexcept {
    return active() ? &state_->simulation : nullptr;
}

const world::World* PreviewRuntime::world() const noexcept {
    return active() ? &state_->world : nullptr;
}

PreviewSnapshot PreviewRuntime::snapshot() const {
    PreviewSnapshot result;
    if (!active()) return result;

    result.playback = state_->playback;
    result.step_index = state_->simulation.step_index();
    result.character_region_events = state_->recent_character_events;
    result.box_region_events = state_->recent_box_events;
    result.activation_requests = state_->recent_activation_requests;

    const auto world_snapshot = state_->world.snapshot();
    result.regions.reserve(world_snapshot.regions.size());
    for (const auto& region : world_snapshot.regions) {
        PreviewRegionSnapshot preview_region{.region = region};
        if (region.bounds.has_value()) {
            preview_region.global_minimum = world::to_global_position(region.bounds->min, region.origin);
            preview_region.global_maximum = world::to_global_position(region.bounds->max, region.origin);
        }
        result.regions.push_back(preview_region);
    }

    for (const auto& record : state_->boxes) {
        const auto entity = state_->world.find(record.owner);
        if (!entity.has_value()) continue;
        const auto spatial = state_->world.spatial_state(*entity);
        if (!spatial.has_value()) continue;
        const auto* region = state_->region_runtime(spatial->region);
        const auto region_desc = state_->world.region(spatial->region);
        const auto local = state_->world.local_position(*entity);
        if (region == nullptr || !region_desc.has_value() || !local.has_value()) continue;
        const auto binding = state_->bindings.find(
            state_->world,
            state_->simulation,
            *entity,
            region->domain->id()
        );
        if (!binding.has_value()) continue;
        auto desc = record.desc;
        desc.participant = *binding;
        const auto body = rigid_body::debug_snapshot(*region->domain, desc);
        if (!body.has_value()) continue;
        result.bodies.push_back(PreviewBodySnapshot{
            .owner = record.owner,
            .region = spatial->region,
            .global_position = spatial->global_position,
            .local_position = *local,
            .body = *body,
        });
    }

    for (const auto& record : state_->characters) {
        const auto entity = state_->world.find(record.owner);
        if (!entity.has_value()) continue;
        const auto spatial = state_->world.spatial_state(*entity);
        if (!spatial.has_value()) continue;
        const auto* region = state_->region_runtime(spatial->region);
        const auto local = state_->world.local_position(*entity);
        const auto character_id = state_->bridge.character_id(state_->world, state_->simulation, *entity);
        if (region == nullptr || !local.has_value() || !character_id.has_value()) continue;
        auto desc = record.desc;
        desc.id = *character_id;
        const auto character = rigid_body::debug_snapshot(*region->domain, desc);
        if (!character.has_value()) continue;
        result.characters.push_back(PreviewCharacterSnapshot{
            .owner = record.owner,
            .region = spatial->region,
            .global_position = spatial->global_position,
            .local_position = *local,
            .character = *character,
        });
    }

    for (const auto& runtime : state_->regions) {
        const auto region_desc = state_->world.region(runtime.region);
        if (!region_desc.has_value()) continue;
        for (const auto& contact : runtime.domain->contact_events()) {
            result.contacts.push_back(PreviewContactSnapshot{
                .region = runtime.region,
                .global_point = global_position(*region_desc, contact.point),
                .contact = contact,
            });
        }
    }

    return result;
}

} // namespace vice::editor
