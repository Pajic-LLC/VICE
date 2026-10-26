#include <vice/rigid_body/authored_simulation.hpp>

#include <algorithm>
#include <cmath>
#include <unordered_map>
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
    world::World* world = nullptr;
    simulation::Coordinator coordinator{1.0 / 120.0};
    simulation::EntityBindings bindings;
    WorldBridge bridge;
    std::vector<Region> regions;
    std::unordered_map<world::RegionId, Domain*, world::RegionIdHash> domain_lookup;
    std::vector<Body> bodies;
    double accumulator = 0;

    const Domain* domain(world::RegionId id) const noexcept {
        const auto found = domain_lookup.find(id);
        return found == domain_lookup.end() ? nullptr : found->second;
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
        if (entity.rigid_box && entity.spatial_state) physical_regions.insert(entity.spatial_state->region);
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
        next->bodies.push_back({entity.persistent_id, desc, *owner, authored, *entity.spatial_state, entity.authored_transform});
    }
    state_ = std::move(next);
    if (diagnostics) diagnostics->clear();
    return true;
}

bool AuthoredSimulation::step(std::string* diagnostics) {
    if (!state_->world) return fail(diagnostics, "Authored simulation has not been initialized.");
    // Reject external body edits before advancing or publishing any solver state.
    for (const auto& record : state_->bodies) {
        const auto entity = state_->world->find(record.owner);
        const auto authored = entity ? state_->world->rigid_box(*entity) : std::nullopt;
        if (!entity || *entity != record.entity || !authored || !same_body(*authored, record.expected_body) ||
            state_->world->spatial_state(*entity) != record.expected_spatial ||
            state_->world->authored_transform(*entity) != record.expected_transform) {
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

const simulation::Coordinator& AuthoredSimulation::coordinator() const noexcept { return state_->coordinator; }
std::size_t AuthoredSimulation::body_count() const noexcept { return state_->bodies.size(); }
const WorldBridge& AuthoredSimulation::bridge() const noexcept { return state_->bridge; }

std::optional<BodyDebugSnapshot> AuthoredSimulation::body(const world::PersistentId owner) const noexcept {
    if (!state_->world) return std::nullopt;
    const auto record = std::find_if(state_->bodies.begin(), state_->bodies.end(), [&](const auto& b) { return b.owner == owner; });
    return record == state_->bodies.end() ? std::nullopt : state_->observed_body(*record);
}
} // namespace vice::rigid_body
