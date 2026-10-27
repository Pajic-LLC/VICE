#include <vice/rigid_body/authored_simulation.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>
#include <tuple>
#include <unordered_set>

namespace vice::rigid_body {
namespace {
bool fail(std::string* diagnostics, std::string message) {
    if (diagnostics) *diagnostics = std::move(message);
    return false;
}
simulation::Fidelity fidelity(world::RegionFidelity value) {
    switch (value) {
    case world::RegionFidelity::inactive: return simulation::Fidelity::off;
    case world::RegionFidelity::reduced: return simulation::Fidelity::reduced;
    case world::RegionFidelity::full: return simulation::Fidelity::full;
    }
    return simulation::Fidelity::off;
}
bool same_body(const world::RigidBox& a, const world::RigidBox& b) {
    const auto same_vector = [](math::Vec3 x, math::Vec3 y) { return x.x == y.x && x.y == y.y && x.z == y.z; };
    return a.motion == b.motion && same_vector(a.half_extents, b.half_extents) &&
        same_vector(a.linear_velocity, b.linear_velocity) && same_vector(a.angular_velocity, b.angular_velocity) &&
        a.friction == b.friction && a.restitution == b.restitution && a.continuous == b.continuous && a.trigger == b.trigger;
}
MotionType motion(world::RigidMotion value) {
    switch (value) {
    case world::RigidMotion::static_body: return MotionType::static_body;
    case world::RigidMotion::kinematic_body: return MotionType::kinematic_body;
    case world::RigidMotion::dynamic_body: return MotionType::dynamic_body;
    }
    return MotionType::static_body;
}
} // namespace

struct AuthoredSimulation::State {
    struct Region { world::RegionId id; std::unique_ptr<Domain> domain; };
    struct Body {
        world::PersistentId owner;
        BoxBodyDesc desc;
        world::EntityId entity;
        world::RigidBox expected_body;
        world::SpatialState expected_spatial;
        std::optional<world::AuthoredTransform> expected_transform;
    };
    struct Controller {
        world::PersistentId owner;
        world::EntityId entity;
        world::Character expected;
        world::SpatialState expected_spatial;
    };
    std::vector<Controller> characters;
    std::unordered_map<world::PersistentId,std::size_t,world::PersistentIdHash> character_lookup;
    std::uint64_t character_revision = 0;
    struct Link {
        world::PersistentId owner;
        world::EntityId entity;
        world::DistanceLink expected;
        world::RegionId region;
        ConstraintId native_id;
    };
    std::vector<Link> links;
    std::unordered_map<std::uint64_t, std::size_t> link_ids;
    std::uint64_t link_revision = 0;
    world::World* world = nullptr;
    simulation::Coordinator coordinator{1.0 / 120.0};
    simulation::EntityBindings bindings;
    WorldBridge bridge;
    std::vector<Region> regions;
    std::unordered_map<world::RegionId, Domain*, world::RegionIdHash> domain_lookup;
    std::unordered_map<std::uint64_t, std::size_t> participant_bodies;
    std::vector<Body> bodies;
    std::unordered_map<world::PersistentId, std::size_t, world::PersistentIdHash> body_lookup;
    double accumulator = 0;
    bool unpublished_step = false;

    const Domain* domain(world::RegionId id) const noexcept {
        const auto found = domain_lookup.find(id);
        return found == domain_lookup.end() ? nullptr : found->second;
    }

    bool matches(const Body& record) const {
        const auto entity = world->find(record.owner);
        const auto authored = entity ? world->rigid_box(*entity) : std::nullopt;
        return entity && *entity == record.entity && authored && same_body(*authored, record.expected_body) &&
            world->spatial_state(*entity) == record.expected_spatial &&
            world->authored_transform(*entity) == record.expected_transform;
    }

    bool matches(const Controller& record) const {
        return world->find(record.owner) == record.entity && world->character(record.entity) == record.expected &&
            world->spatial_state(record.entity) == record.expected_spatial;
    }
    std::optional<CharacterObservation> observed_character(const Controller& record) const noexcept {
        const auto spatial = world->spatial_state(record.entity);
        const auto* target = spatial ? domain(spatial->region) : nullptr;
        if (!target) return std::nullopt;
        const auto native = bridge.character_id(*world,coordinator,record.entity);
        const auto state = native ? target->character_state(*native) : std::nullopt;
        if (!state) return std::nullopt;
        CharacterObservation result{*state,{},*native};
        if (state->ground_participant) {
            const auto body = participant_bodies.find(state->ground_participant->value);
            if (body != participant_bodies.end()) result.supporting_entity = bodies[body->second].owner;
        }
        return result;
    }
    bool publish_character(Controller& record) {
        const auto spatial = world->spatial_state(record.entity);
        // A transferred controller has new contact caches. Reconstruct support
        // before publishing so the next SDK read does not invent an airborne tick.
        if (spatial && spatial->region != record.expected_spatial.region) {
            const auto native = bridge.character_id(*world,coordinator,record.entity);
            auto* target = domain_lookup.at(spatial->region);
            const auto current = native ? target->character_state(*native) : std::nullopt;
            if (!current || !target->set_character_position(*native,current->position)) return false;
        }
        const auto observed = observed_character(record);
        if (!observed || !spatial) return false;
        auto c = record.expected;
        c.velocity = observed->state.velocity; c.horizontal_velocity = observed->state.horizontal_velocity;
        c.pending_jump_speed = observed->state.pending_jump_speed;
        if (!world->set_character(record.entity,c)) return false;
        record.expected = c; record.expected_spatial = *spatial;
        return true;
    }
    std::optional<BodyDebugSnapshot> observed_body(const Body& record) const noexcept {
        if (!world) return std::nullopt;
        const auto entity = world->find(record.owner);
        if (!entity) return std::nullopt;
        const auto spatial = world->spatial_state(*entity);
        if (!spatial) return std::nullopt;
        const auto* target = domain(spatial->region);
        if (!target) return std::nullopt;
        const auto participant = bindings.find(*world, coordinator, *entity, target->id());
        if (!participant) return std::nullopt;
        auto desc = record.desc;
        desc.participant = *participant;
        return debug_snapshot(*target, desc);
    }
};

AuthoredSimulation::AuthoredSimulation() : state_{std::make_unique<State>()} {}
AuthoredSimulation::~AuthoredSimulation() = default;

bool AuthoredSimulation::reset(world::World& world, jobs::Scheduler& jobs, std::string* diagnostics) {
    auto next = std::make_unique<State>();
    next->world = &world;
    const auto snapshot = world.snapshot();
    std::unordered_set<world::RegionId, world::RegionIdHash> physical_regions;
    for (const auto& entity : snapshot.entities) {
        if ((entity.rigid_box || entity.character) && entity.spatial_state) physical_regions.insert(entity.spatial_state->region);
    }
    next->regions.reserve(snapshot.regions.size());
    for (const auto& region : snapshot.regions) {
        if (!physical_regions.contains(region.id)) continue;
        auto domain = Domain::create(simulation::DomainId{region.id.value},
            "authored.region." + std::to_string(region.id.value), jobs);
        if (!domain) return fail(diagnostics, "Could not create authored rigid-body domain.");
        domain->set_fidelity(fidelity(region.fidelity));
        if (!next->coordinator.register_domain(*domain) ||
            !next->bridge.register_region(world, next->coordinator, region.id, *domain)) {
            return fail(diagnostics, "Authored simulation requires one distinct simulation island per physical region.");
        }
        next->domain_lookup.emplace(region.id, domain.get());
        next->regions.push_back({region.id, std::move(domain)});
    }
    for (const auto& entity : snapshot.entities) {
        if (!entity.rigid_box) continue;
        const auto owner = world.find(entity.persistent_id);
        if (!owner || !entity.spatial_state || !world::valid(*entity.rigid_box)) {
            return fail(diagnostics, "Invalid authored rigid box: " + entity.persistent_id.to_string());
        }
        const auto& authored = *entity.rigid_box;
        const auto transform = entity.authored_transform.value_or(world::AuthoredTransform{});
        BoxBodyDesc desc;
        desc.participant = simulation::ParticipantId{static_cast<std::uint64_t>(next->bodies.size()) + 1U};
        desc.motion = motion(authored.motion);
        desc.motion_quality = authored.continuous ? MotionQuality::continuous : MotionQuality::discrete;
        desc.half_extents = {authored.half_extents.x * transform.scale.x,
            authored.half_extents.y * transform.scale.y, authored.half_extents.z * transform.scale.z};
        desc.orientation = {transform.rotation.x, transform.rotation.y, transform.rotation.z, transform.rotation.w};
        desc.linear_velocity = authored.linear_velocity;
        desc.angular_velocity = authored.angular_velocity;
        desc.friction = authored.friction;
        desc.restitution = authored.restitution;
        desc.trigger = authored.trigger;
        if (!next->bridge.create_box(world, next->coordinator, next->bindings, *owner, desc)) {
            return fail(diagnostics, "Could not instantiate authored rigid box: " + entity.persistent_id.to_string());
        }
        next->body_lookup.emplace(entity.persistent_id, next->bodies.size());
        next->participant_bodies.emplace(desc.participant.value, next->bodies.size());
        next->bodies.push_back({entity.persistent_id, desc, *owner, authored, *entity.spatial_state, entity.authored_transform});
    }
    for (const auto& entity : snapshot.entities) {
        if (!entity.distance_link) continue;
        const auto owner = world.find(entity.persistent_id);
        const auto& link = *entity.distance_link;
        State::Link record{entity.persistent_id, *owner, link, {}, {static_cast<std::uint64_t>(next->links.size()) + 1U}};
        if (!link.broken) {
            const auto first = next->body_lookup.find(link.first), second = next->body_lookup.find(link.second);
            if (first == next->body_lookup.end() || second == next->body_lookup.end()) return fail(diagnostics, "Distance link endpoints require authored rigid boxes.");
            const auto& a = next->bodies[first->second]; const auto& b = next->bodies[second->second];
            record.region = a.expected_spatial.region;
            const auto target = next->domain_lookup.find(record.region);
            if (target == next->domain_lookup.end() || record.region != b.expected_spatial.region) return fail(diagnostics, "Distance link endpoints require one physical region.");
            DistanceConstraintDesc desc;
            desc.id = record.native_id; desc.first = a.desc.participant; desc.second = b.desc.participant;
            desc.min_distance = link.min_distance; desc.max_distance = link.max_distance;
            desc.limits_spring = {link.frequency_hz, link.damping_ratio};
            if (link.break_threshold > 0) desc.break_thresholds.linear_impulse = link.break_threshold;
            if (!target->second->create_constraint(desc)) return fail(diagnostics, "Could not instantiate distance link: " + entity.persistent_id.to_string());
        }
        next->link_ids.emplace(record.native_id.value, next->links.size());
        next->links.push_back(record);
    }
    for (const auto& entity : snapshot.entities) {
        if (!entity.character) continue;
        const auto owner = world.find(entity.persistent_id);
        const auto& c = *entity.character;
        if (!owner || !entity.spatial_state || !world::valid(c) || next->characters.size() >= std::numeric_limits<std::uint32_t>::max()-1U)
            return fail(diagnostics,"Invalid authored character: " + entity.persistent_id.to_string());
        auto* target = next->domain_lookup.at(entity.spatial_state->region);
        CharacterDesc desc;
        desc.id = CharacterId{static_cast<std::uint32_t>(next->characters.size()+1U)};
        desc.height=c.height; desc.radius=c.radius; desc.mass=c.mass; desc.max_strength=c.max_strength;
        desc.max_slope_angle=c.max_slope_angle; desc.step_height=c.step_height; desc.stick_to_floor_distance=c.stick_to_floor_distance;
        if (!next->bridge.create_character(world,next->coordinator,*owner,desc)) return fail(diagnostics,"Could not instantiate authored character.");
        const auto native = next->bridge.character_id(world,next->coordinator,*owner);
        const auto initial = native ? target->character_state(*native) : std::nullopt;
        if (!native || !initial || !target->set_character_position(*native,initial->position) ||
            !target->set_character_velocity(*native,c.velocity) || !target->set_character_horizontal_velocity(*native,c.horizontal_velocity) ||
            (c.pending_jump_speed && !target->request_character_jump(*native,*c.pending_jump_speed))) return fail(diagnostics,"Could not restore authored character motion.");
        next->character_lookup.emplace(entity.persistent_id,next->characters.size());
        next->characters.push_back({entity.persistent_id,*owner,c,*entity.spatial_state});
    }
    next->character_revision = world.character_revision();
    next->link_revision = world.distance_link_revision();
    if(!world.begin_causal_recording(true)) return fail(diagnostics,"Causal event sequence is exhausted.");
    state_ = std::move(next);
    if (diagnostics) diagnostics->clear();
    return true;
}

bool AuthoredSimulation::step(std::string* diagnostics) {
    if (!state_->world) return fail(diagnostics, "Authored simulation has not been initialized.");
    if(state_->unpublished_step) return fail(diagnostics,"The previous solver step could not publish; reset the simulation.");
    if (state_->world->distance_link_revision() != state_->link_revision) return fail(diagnostics, "Distance links were edited externally; reset the simulation.");
    for (const auto& link : state_->links) {
        if (state_->world->find(link.owner) != link.entity || state_->world->distance_link(link.entity) != link.expected)
            return fail(diagnostics, "Distance link state changed externally; reset the simulation.");
    }
    if (state_->world->character_revision() != state_->character_revision) return fail(diagnostics,"Characters were edited externally; reset the simulation.");
    for (const auto& record : state_->characters) if (!state_->matches(record)) return fail(diagnostics,"Character state changed externally; reset the simulation.");
    // Reject external body edits before advancing or publishing any solver state.
    for (const auto& record : state_->bodies) {
        if (!state_->matches(record)) {
            return fail(diagnostics, "Authored body state was edited externally; reset the simulation.");
        }
    }
    // Apply explicit World fidelity changes before the next solver step.
    for (auto& region : state_->regions) {
        const auto current = state_->world->region(region.id);
        if (!current) return fail(diagnostics, "An authored simulation region was removed; reset the simulation.");
        region.domain->set_fidelity(fidelity(current->fidelity));
    }
    state_->coordinator.step();
    state_->unpublished_step=true;
    // Publish broken identity/state before handoff can move the released endpoints.
    for (const auto& region : state_->regions) {
        for (const auto& event : region.domain->constraint_break_events()) {
            const auto found = state_->link_ids.find(event.id.value);
            if (found == state_->link_ids.end()) continue;
            auto& link = state_->links[found->second];
            if (link.region != region.id || link.expected.broken) continue;
            auto broken = link.expected;
            broken.broken = true; broken.last_break_impulse = event.linear_impulse;
            broken.break_step = state_->coordinator.step_index();
            if (!state_->world->publish_distance_link_break(link.entity,event.linear_impulse,state_->coordinator.step_index())) return fail(diagnostics, "Could not publish broken distance link state.");
            link.expected = broken;
        }
    }
    state_->link_revision = state_->world->distance_link_revision();
    state_->bridge.orchestrate_boxes(*state_->world, state_->coordinator, state_->bindings);
    for (auto& record : state_->bodies) {
        const auto entity = state_->world->find(record.owner);
        if (!entity) return fail(diagnostics, "An authored physics entity was removed; reset the simulation.");
        const auto observed = state_->observed_body(record);
        const auto authored = state_->world->rigid_box(*entity);
        if (!observed || !authored) return fail(diagnostics, "Authored body state changed structurally; reset the simulation.");
        auto current = *authored;
        current.linear_velocity = observed->linear_velocity;
        current.angular_velocity = observed->angular_velocity;
        auto transform = state_->world->authored_transform(*entity).value_or(world::AuthoredTransform{});
        transform.rotation = {observed->orientation.x, observed->orientation.y, observed->orientation.z, observed->orientation.w};
        if (!state_->world->set_rigid_box(*entity, current) || !state_->world->set_authored_transform(*entity, transform)) {
            return fail(diagnostics, "Could not publish authoritative rigid-body state.");
        }
        record.expected_body = current;
        record.expected_spatial = *state_->world->spatial_state(*entity);
        record.expected_transform = state_->world->authored_transform(*entity);
    }
    state_->bridge.orchestrate_characters(*state_->world,state_->coordinator);
    for (auto& c : state_->characters) if (!state_->publish_character(c)) return fail(diagnostics,"Could not publish authoritative character state.");
    state_->character_revision = state_->world->character_revision();
    state_->unpublished_step=false;
    if (diagnostics) diagnostics->clear();
    return true;
}

bool AuthoredSimulation::advance(const double delta_seconds, std::string* diagnostics) {
    if (!state_->world) return fail(diagnostics, "Authored simulation has not been initialized.");
    if (!std::isfinite(delta_seconds) || delta_seconds < 0) return fail(diagnostics, "Simulation delta must be finite and nonnegative.");
    state_->accumulator += std::min(delta_seconds, 0.25);
    std::size_t steps = 0;
    while (state_->accumulator >= state_->coordinator.fixed_delta_seconds() && steps < 8U) {
        if (!step(diagnostics)) return false;
        state_->accumulator -= state_->coordinator.fixed_delta_seconds();
        ++steps;
    }
    if (steps == 8U) state_->accumulator = 0;
    return true;
}

BodyControlResult AuthoredSimulation::set_body_velocity(const world::PersistentId owner, const math::Vec3 linear, const math::Vec3 angular) {
    const auto finite = [](math::Vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); };
    if (!finite(linear) || !finite(angular)) return BodyControlResult::invalid_argument;
    const auto found = state_->body_lookup.find(owner);
    if (!state_->world || found == state_->body_lookup.end()) return BodyControlResult::not_found;
    auto& record = state_->bodies[found->second];
    if (!state_->matches(record)) return BodyControlResult::reset_required;
    if (record.expected_body.motion == world::RigidMotion::static_body) return BodyControlResult::static_body;
    const auto region = state_->world->region(record.expected_spatial.region);
    if (!region) return BodyControlResult::reset_required;
    if (region->fidelity == world::RegionFidelity::inactive) return BodyControlResult::inactive_region;
    const auto target = state_->domain_lookup.find(region->id);
    if (target == state_->domain_lookup.end()) return BodyControlResult::reset_required;
    const auto participant = state_->bindings.find(*state_->world, state_->coordinator, record.entity, target->second->id());
    if (!participant || !target->second->set_body_velocity(*participant, linear, angular)) return BodyControlResult::reset_required;
    const auto observed = target->second->body_state(*participant);
    if (!observed) return BodyControlResult::reset_required;
    auto current = record.expected_body;
    current.linear_velocity = observed->linear_velocity;
    current.angular_velocity = observed->angular_velocity;
    if (!state_->world->set_rigid_box(record.entity, current)) return BodyControlResult::reset_required;
    record.expected_body = current;
    return BodyControlResult::applied;
}

std::size_t AuthoredSimulation::character_count() const noexcept { return state_->characters.size(); }
std::optional<CharacterObservation> AuthoredSimulation::character(world::PersistentId owner) const noexcept {
    if (!state_->world || state_->world->character_revision() != state_->character_revision) return std::nullopt;
    const auto found = state_->character_lookup.find(owner);
    if (found == state_->character_lookup.end() || !state_->matches(state_->characters[found->second])) return std::nullopt;
    return state_->observed_character(state_->characters[found->second]);
}
CharacterControlResult AuthoredSimulation::control_character(world::PersistentId owner, math::Vec3 horizontal, float jump_speed) {
    const auto finite = [](math::Vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); };
    if (!finite(horizontal) || horizontal.y != 0 || !std::isfinite(jump_speed) || jump_speed < 0) return CharacterControlResult::invalid_argument;
    if (!state_->world) return CharacterControlResult::not_found;
    const auto found = state_->character_lookup.find(owner);
    if (found == state_->character_lookup.end()) return CharacterControlResult::not_found;
    auto& record = state_->characters[found->second];
    if (state_->world->character_revision() != state_->character_revision || !state_->matches(record)) return CharacterControlResult::reset_required;
    const auto region = state_->world->region(record.expected_spatial.region);
    if (!region) return CharacterControlResult::reset_required;
    if (region->fidelity == world::RegionFidelity::inactive) return CharacterControlResult::inactive_region;
    const auto observed = state_->observed_character(record);
    auto* target = state_->domain_lookup.at(region->id);
    const auto native = state_->bridge.character_id(*state_->world,state_->coordinator,record.entity);
    if (!observed || !native) return CharacterControlResult::reset_required;
    if (jump_speed > 0 && (observed->state.ground_state != CharacterGroundState::grounded || observed->state.pending_jump_speed ||
        observed->state.velocity.y-observed->state.ground_velocity.y >= 0.1F)) return CharacterControlResult::not_grounded;
    if (!target->set_character_horizontal_velocity(*native,horizontal) ||
        (jump_speed > 0 && !target->request_character_jump(*native,jump_speed)) || !state_->publish_character(record)) return CharacterControlResult::reset_required;
    state_->character_revision = state_->world->character_revision();
    return CharacterControlResult::applied;
}

const simulation::Coordinator& AuthoredSimulation::coordinator() const noexcept { return state_->coordinator; }
std::size_t AuthoredSimulation::body_count() const noexcept { return state_->bodies.size(); }
const WorldBridge& AuthoredSimulation::bridge() const noexcept { return state_->bridge; }

std::optional<BodyDebugSnapshot> AuthoredSimulation::body(const world::PersistentId owner) const noexcept {
    if (!state_->world) return std::nullopt;
    const auto record = state_->body_lookup.find(owner);
    return record == state_->body_lookup.end() ? std::nullopt : state_->observed_body(state_->bodies[record->second]);
}
std::optional<BodySupport> AuthoredSimulation::body_support(const world::PersistentId owner, const math::Vec3 up, const float min_up_dot) const noexcept {
    const double length = std::hypot(static_cast<double>(up.x), static_cast<double>(up.y), static_cast<double>(up.z));
    if (!std::isfinite(length) || length <= 0 || !std::isfinite(min_up_dot) || min_up_dot < -1 || min_up_dot > 1) return std::nullopt;
    const auto found = state_->body_lookup.find(owner);
    if (!state_->world || found == state_->body_lookup.end()) return std::nullopt;
    const auto& record = state_->bodies[found->second];
    if (!state_->matches(record)) return std::nullopt;
    const auto region = state_->world->region(record.expected_spatial.region);
    if (!region) return std::nullopt;
    if (region->fidelity == world::RegionFidelity::inactive || record.expected_body.trigger) return BodySupport{};
    const auto domain = state_->domain_lookup.find(region->id);
    if (domain == state_->domain_lookup.end()) return std::nullopt;
    const auto participant = state_->bindings.find(*state_->world, state_->coordinator, record.entity, domain->second->id());
    if (!participant) return std::nullopt;
    const math::Vec3 direction{static_cast<float>(up.x / length), static_cast<float>(up.y / length), static_cast<float>(up.z / length)};
    BodySupport result;
    float best = min_up_dot;
    const QueryFilter solid_only{.include_triggers = false};
    for (const auto& contact : domain->second->body_contacts(*participant, 0.02F, solid_only)) {
        const auto other = state_->participant_bodies.find(contact.participant.value);
        if (other == state_->participant_bodies.end()) continue;
        const auto& supporting = state_->bodies[other->second];
        if (!state_->matches(supporting)) return std::nullopt;
        if (supporting.expected_spatial.region != region->id) continue;
        const auto other_state = domain->second->body_state(contact.participant);
        if (!other_state) continue;
        const float alignment = math::dot(contact.normal, direction);
        if (alignment < best) continue;
        const auto angular = math::cross(other_state->angular_velocity,
            {contact.point.x - other_state->position.x, contact.point.y - other_state->position.y, contact.point.z - other_state->position.z});
        const BodySupport candidate{true, supporting.owner,
            {region->origin.x + contact.point.x, region->origin.y + contact.point.y, region->origin.z + contact.point.z}, contact.normal,
            {other_state->linear_velocity.x + angular.x, other_state->linear_velocity.y + angular.y, other_state->linear_velocity.z + angular.z}};
        if (result.supported && alignment == best &&
            !(std::tuple{candidate.supporting_entity.bytes(), candidate.point.x, candidate.point.y, candidate.point.z} <
              std::tuple{result.supporting_entity.bytes(), result.point.x, result.point.y, result.point.z})) continue;
        result = candidate;
        best = alignment;
    }
    return result;
}
} // namespace vice::rigid_body
