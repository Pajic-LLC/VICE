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
SwingTwistMotorDesc swing_motor(const world::SwingTwistDrive &d) {
    return {static_cast<ConstraintMotorMode>(d.swing_mode),
            static_cast<ConstraintMotorMode>(d.twist_mode),
            {d.target_orientation[0], d.target_orientation[1], d.target_orientation[2],
             d.target_orientation[3]},
            {d.target_velocity[0], d.target_velocity[1], d.target_velocity[2]},
            {d.frequency_hz, d.damping_ratio},
            d.max_swing_axis_torque,
            d.max_twist_torque};
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
        std::optional<world::CharacterAttachment> attachment;
    };
    struct VehicleRecord {
        world::PersistentId owner;
        world::EntityId entity;
        world::Vehicle expected;
        VehicleObservation observed;
    };
    std::vector<VehicleRecord> vehicles;
    std::unordered_map<world::PersistentId,std::size_t,world::PersistentIdHash> vehicle_lookup;
    std::uint64_t vehicle_revision = 0;
    bool drive_vehicles();
    bool publish_vehicles();
    std::vector<Controller> characters;
    std::unordered_map<world::PersistentId,std::size_t,world::PersistentIdHash> character_lookup;
    std::uint64_t character_revision = 0;
    std::uint64_t attachment_revision = 0;
    bool controllers_current() const;
    std::uint64_t material_revision = 0;
    struct Link {
        world::PersistentId owner;
        world::EntityId entity;
        world::DistanceLink expected;
        world::RegionId region;
        ConstraintId native_id;
    };
    struct Joint {
        world::PersistentId owner;
        world::EntityId entity;
        world::PoweredHinge expected;
        world::RegionId region;
        ConstraintId native_id;
    };
    std::uint64_t joint_revision = 0;
    struct SwingJoint {
        world::PersistentId owner;
        world::EntityId entity;
        world::PoweredSwingTwist expected;
        world::RegionId region;
        ConstraintId native_id;
    };
    std::vector<SwingJoint> swing_joints;
    std::unordered_map<world::PersistentId, std::size_t, world::PersistentIdHash> swing_lookup;
    std::vector<Joint> joints;
    std::unordered_map<world::PersistentId, std::size_t, world::PersistentIdHash> joint_lookup;
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
        return world->material_revision()==material_revision && entity && *entity == record.entity && authored && same_body(*authored, record.expected_body) &&
            world->spatial_state(*entity) == record.expected_spatial &&
            world->authored_transform(*entity) == record.expected_transform;
    }

    bool matches(const Controller& record) const {
        if(record.attachment) {
            const auto parent=body_lookup.find(record.attachment->carrier);
            if(parent==body_lookup.end() || !matches(bodies[parent->second]))return false;
        }
        return world->material_revision()==material_revision && world->find(record.owner) == record.entity && world->character(record.entity) == record.expected &&
            world->spatial_state(record.entity) == record.expected_spatial && world->character_attachment(record.entity)==record.attachment;
    }
    std::optional<CharacterObservation> observed_character(const Controller& record) const noexcept {
        const auto spatial = world->spatial_state(record.entity);
        if(record.attachment) {
            const auto local=world->local_position(record.entity);if(!local)return std::nullopt;
            CharacterState state;state.position={local->x,local->y,local->z};state.velocity=record.expected.velocity;state.ground_state=CharacterGroundState::unsupported;
            return CharacterObservation{state,{},{}};
        }
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
        if(record.attachment) {
            if(!world->sync_character_attachment(record.entity))return false;
            record.expected=*world->character(record.entity);record.expected_spatial=*world->spatial_state(record.entity);return true;
        }
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
        if (!world || world->material_revision()!=material_revision) return std::nullopt;
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

#include "authored_vehicles.inc"
#include "authored_attachments.inc"

AuthoredSimulation::AuthoredSimulation() : state_{std::make_unique<State>()} {}
AuthoredSimulation::~AuthoredSimulation() = default;

bool AuthoredSimulation::reset(world::World& world, jobs::Scheduler& jobs, std::string* diagnostics) {
    auto next = std::make_unique<State>();
    next->world = &world;
    const auto snapshot = world.snapshot();
    next->material_revision=world.material_revision();
    std::unordered_map<world::PersistentId,simulation::MaterialId,world::PersistentIdHash> material_ids;
    for(std::size_t i=0;i<snapshot.materials.size();++i) material_ids.emplace(snapshot.materials[i].id,simulation::MaterialId{static_cast<std::uint64_t>(i)+1U});
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
        for(const auto& definition:snapshot.materials) {
            simulation::Material material;
            if(!material.set_property(std::string{material_property_owner},std::string{material_friction_property},static_cast<double>(definition.friction)) ||
               !material.set_property(std::string{material_property_owner},std::string{material_restitution_property},static_cast<double>(definition.restitution)) ||
               !domain->register_material(material_ids.at(definition.id),material)) return fail(diagnostics,"Could not register shared physical material.");
        }
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
        const auto surface=world.material_surface(*owner);
        if(!surface) return fail(diagnostics,"Invalid physical material surface.");
        desc.density=surface->density;desc.friction=surface->friction;desc.restitution=surface->restitution;
        if(!surface->id.is_nil())desc.material=material_ids.at(surface->id);
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
    for (const auto &e : snapshot.entities)
        if (e.powered_hinge) {
            const auto &h = *e.powered_hinge;
            const auto a = next->body_lookup.find(h.first), b = next->body_lookup.find(h.second);
            if (a == next->body_lookup.end() || b == next->body_lookup.end())
                return fail(diagnostics, "Powered hinge endpoints require authored bodies.");
            const auto &first = next->bodies[a->second];
            const auto &second = next->bodies[b->second];
            if (first.expected_spatial.region != second.expected_spatial.region)
                return fail(diagnostics, "Powered hinge endpoints require one region.");
            State::Joint joint{
                e.persistent_id,
                *world.find(e.persistent_id),
                h,
                first.expected_spatial.region,
                {static_cast<std::uint64_t>(next->links.size() + next->joints.size() + 1)}};
            HingeConstraintDesc d;
            d.collide_connected = false;
            d.id = joint.native_id;
            d.first = first.desc.participant;
            d.second = second.desc.participant;
            d.min_angle = h.min_angle;
            d.max_angle = h.max_angle;
            d.enabled = h.enabled;
            d.local_frames = std::array{
                HingeLocalFrame{h.first_frame.anchor, h.first_frame.axis, h.first_frame.normal},
                HingeLocalFrame{h.second_frame.anchor, h.second_frame.axis, h.second_frame.normal}};
            d.motor = {static_cast<ConstraintMotorMode>(h.drive.mode),
                       h.drive.target_velocity,
                       h.drive.target_angle,
                       {h.drive.frequency_hz, h.drive.damping_ratio},
                       h.drive.max_torque};
            if (!next->domain_lookup.at(joint.region)->create_constraint(d))
                return fail(diagnostics, "Could not instantiate powered hinge.");
            next->joint_lookup.emplace(e.persistent_id, next->joints.size());
            next->joints.push_back(joint);
        }
    for (const auto &e : snapshot.swing_twist_joints) {
        const auto &h = e.joint;
        const auto a = next->body_lookup.find(h.first), b = next->body_lookup.find(h.second);
        if (a == next->body_lookup.end() || b == next->body_lookup.end())
            return fail(diagnostics, "Swing/twist body is missing.");
        const auto &first = next->bodies[a->second];
        const auto &second = next->bodies[b->second];
        if (first.expected_spatial.region != second.expected_spatial.region)
            return fail(diagnostics, "Swing/twist bodies require a common region.");
        State::SwingJoint j{e.owner,
                            *world.find(e.owner),
                            h,
                            first.expected_spatial.region,
                            {static_cast<std::uint64_t>(next->links.size() + next->joints.size() +
                                                        next->swing_joints.size() + 1)}};
        SwingTwistConstraintDesc d;
        d.id = j.native_id;
        d.first = first.desc.participant;
        d.second = second.desc.participant;
        d.enabled = h.enabled;
        d.local_frames = {
            HingeLocalFrame{h.first_frame.anchor, h.first_frame.axis, h.first_frame.normal},
            HingeLocalFrame{h.second_frame.anchor, h.second_frame.axis, h.second_frame.normal}};
        d.swing_half_angle = h.configuration.swing_half_angle;
        d.twist_min = h.configuration.twist_min;
        d.twist_max = h.configuration.twist_max;
        d.motor = swing_motor(h.configuration.drive);
        if (!next->domain_lookup.at(j.region)->create_constraint(d))
            return fail(diagnostics, "Could not instantiate swing/twist joint.");
        next->swing_lookup.emplace(e.owner, next->swing_joints.size());
        next->swing_joints.push_back(j);
    }
    for (const auto& entity : snapshot.entities) {
        if (!entity.character) continue;
        const auto owner = world.find(entity.persistent_id);
        const auto& c = *entity.character;
        if (!owner || !entity.spatial_state || !world::valid(c) || next->characters.size() >= std::numeric_limits<std::uint32_t>::max()-1U)
            return fail(diagnostics,"Invalid authored character: " + entity.persistent_id.to_string());
        if(!entity.character_attachment) {
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
        }
        next->character_lookup.emplace(entity.persistent_id,next->characters.size());
        next->characters.push_back({entity.persistent_id,*owner,c,*entity.spatial_state,entity.character_attachment});
    }
    for(const auto& entity:snapshot.entities) if(entity.vehicle) {
        const auto owner=world.find(entity.persistent_id);
        if(!owner || !next->body_lookup.contains(entity.persistent_id) || !world::valid(*entity.vehicle))return fail(diagnostics,"Invalid authored vehicle.");
        next->vehicle_lookup.emplace(entity.persistent_id,next->vehicles.size());
        next->vehicles.push_back({entity.persistent_id,*owner,*entity.vehicle,VehicleObservation{*entity.vehicle,0,{}}});
    }
    next->attachment_revision=world.attachment_revision();
    next->vehicle_revision=world.vehicle_revision();
    next->character_revision = world.character_revision();
    next->joint_revision = world.powered_hinge_revision();
    next->link_revision = world.distance_link_revision();
    if(!world.begin_causal_recording(true)) return fail(diagnostics,"Causal event sequence is exhausted.");
    state_ = std::move(next);
    if (diagnostics) diagnostics->clear();
    return true;
}

bool AuthoredSimulation::step(std::string* diagnostics) {
    if (!state_->world) return fail(diagnostics, "Authored simulation has not been initialized.");
    if(state_->world->material_revision()!=state_->material_revision)return fail(diagnostics,"Shared materials changed; reset the simulation.");
    if(state_->unpublished_step) return fail(diagnostics,"The previous solver step could not publish; reset the simulation.");
    if (state_->world->distance_link_revision() != state_->link_revision) return fail(diagnostics, "Distance links were edited externally; reset the simulation.");
    for (const auto& link : state_->links) {
        if (state_->world->find(link.owner) != link.entity || state_->world->distance_link(link.entity) != link.expected)
            return fail(diagnostics, "Distance link state changed externally; reset the simulation.");
    }
    if(state_->world->attachment_revision()!=state_->attachment_revision)return fail(diagnostics,"Character attachments changed; reset the simulation.");
    if (state_->world->character_revision() != state_->character_revision) return fail(diagnostics,"Characters were edited externally; reset the simulation.");
    for (const auto& record : state_->characters) if (!state_->matches(record)) return fail(diagnostics,"Character state changed externally; reset the simulation.");
    if(state_->world->vehicle_revision()!=state_->vehicle_revision)return fail(diagnostics,"Vehicles were edited externally; reset the simulation.");
    for(const auto& v:state_->vehicles) if(state_->world->find(v.owner)!=v.entity || state_->world->vehicle(v.entity)!=v.expected)
        return fail(diagnostics,"Vehicle state changed externally; reset the simulation.");
    // Reject external body edits before advancing or publishing any solver state.
    for (const auto& record : state_->bodies) {
        if (!state_->matches(record)) {
            return fail(diagnostics, "Authored body state was edited externally; reset the simulation.");
        }
    }
    if (state_->world->powered_hinge_revision() != state_->joint_revision)
        return fail(diagnostics, "Joint topology changed; reset simulation.");
    // Validate every joint before mutating any native actuator. Structural changes require reset.
    for (const auto &j : state_->joints) {
        const auto id = state_->world->find(j.owner);
        const auto h = id ? state_->world->powered_hinge(*id) : std::nullopt;
        if (!id || *id != j.entity || !h)
            return fail(diagnostics, "Powered hinge changed externally; reset simulation.");
        auto config = *h;
        config.drive = j.expected.drive;
        config.enabled = j.expected.enabled;
        if (config != j.expected)
            return fail(diagnostics,
                        "Powered hinge reference frames/endpoints changed; reset simulation.");
    }
    for (const auto &j : state_->swing_joints) {
        const auto id = state_->world->find(j.owner);
        const auto h = id ? state_->world->swing_twist_joint(*id) : std::nullopt;
        if (!id || *id != j.entity || !h)
            return fail(diagnostics, "Swing/twist topology changed; reset simulation.");
        auto config = *h;
        config.configuration.drive = j.expected.configuration.drive;
        config.enabled = j.expected.enabled;
        if (config != j.expected)
            return fail(diagnostics, "Swing/twist frames or limits changed; reset simulation.");
    }
    for (auto &j : state_->joints) {
        const auto h = *state_->world->powered_hinge(j.entity);
        if (h != j.expected) {
            auto *domain = state_->domain_lookup.at(j.region);
            const auto &v = h.drive;
            if (!domain->set_hinge_motor(j.native_id, {static_cast<ConstraintMotorMode>(v.mode),
                                                       v.target_velocity,
                                                       v.target_angle,
                                                       {v.frequency_hz, v.damping_ratio},
                                                       v.max_torque}) ||
                !domain->set_constraint_enabled(j.native_id, h.enabled))
                return fail(diagnostics, "Could not apply joint drive.");
            j.expected = h;
        }
    }
    for (auto &j : state_->swing_joints) {
        const auto h = *state_->world->swing_twist_joint(j.entity);
        if (h == j.expected)
            continue;
        auto *d = state_->domain_lookup.at(j.region);
        if (!d->set_swing_twist_motor(j.native_id, swing_motor(h.configuration.drive)) ||
            !d->set_constraint_enabled(j.native_id, h.enabled)) {
            state_->unpublished_step = true;
            return fail(diagnostics, "Could not apply swing/twist drive; reset simulation.");
        }
        j.expected = h;
    }
    // Apply explicit World fidelity changes before the next solver step.
    for (auto& region : state_->regions) {
        const auto current = state_->world->region(region.id);
        if (!current) return fail(diagnostics, "An authored simulation region was removed; reset the simulation.");
        region.domain->set_fidelity(fidelity(current->fidelity));
    }
    if(!state_->drive_vehicles()) {state_->unpublished_step=true;return fail(diagnostics,"Could not apply vehicle forces; reset the simulation.");}
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
    if(!state_->publish_vehicles())return fail(diagnostics,"Could not publish vehicle state; reset the simulation.");
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
    if(state_->unpublished_step || state_->world->attachment_revision()!=state_->attachment_revision)return BodyControlResult::reset_required;
    for(const auto& c:state_->characters)if(c.attachment && c.attachment->carrier==owner && !state_->matches(c))return BodyControlResult::reset_required;
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
    for(auto& c:state_->characters)if(c.attachment && c.attachment->carrier==owner) {
        if(!state_->publish_character(c)){state_->unpublished_step=true;return BodyControlResult::reset_required;}
    }
    state_->character_revision=state_->world->character_revision();
    return BodyControlResult::applied;
}

std::size_t AuthoredSimulation::character_count() const noexcept { return state_->characters.size(); }
std::optional<CharacterObservation> AuthoredSimulation::character(world::PersistentId owner) const noexcept {
    if (!state_->world || state_->unpublished_step || state_->world->character_revision() != state_->character_revision || state_->world->attachment_revision()!=state_->attachment_revision) return std::nullopt;
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
    if (state_->unpublished_step || state_->world->character_revision() != state_->character_revision || state_->world->attachment_revision()!=state_->attachment_revision || !state_->matches(record)) return CharacterControlResult::reset_required;
    if(record.attachment)return CharacterControlResult::attached;
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
    if (!state_->world || state_->unpublished_step) return std::nullopt;
    const auto record = state_->body_lookup.find(owner);
    if(record==state_->body_lookup.end() || !state_->matches(state_->bodies[record->second]))return std::nullopt;
    return state_->observed_body(state_->bodies[record->second]);
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

namespace vice::rigid_body {
std::optional<PoweredHingeObservation>
AuthoredSimulation::powered_hinge(world::PersistentId owner) const noexcept {
    if (!state_->world || state_->unpublished_step ||
        state_->world->powered_hinge_revision() != state_->joint_revision)
        return {};
    const auto i = state_->joint_lookup.find(owner);
    if (i == state_->joint_lookup.end())
        return {};
    const auto &j = state_->joints[i->second];
    const auto id = state_->world->find(owner);
    const auto h = id ? state_->world->powered_hinge(*id) : std::nullopt;
    if (!h || *id != j.entity)
        return {};
    auto config = *h;
    config.drive = j.expected.drive;
    config.enabled = j.expected.enabled;
    if (config != j.expected)
        return {};
    const auto a = state_->body_lookup.find(j.expected.first),
               b = state_->body_lookup.find(j.expected.second);
    if (a == state_->body_lookup.end() || b == state_->body_lookup.end() ||
        !state_->matches(state_->bodies[a->second]) || !state_->matches(state_->bodies[b->second]))
        return {};
    const auto s = state_->domain(j.region)->constraint_state(j.native_id);
    if (!s || !s->coordinate)
        return {};
    const auto region = state_->world->region(j.region);
    return PoweredHingeObservation{*h,
                                   *s->coordinate,
                                   h->drive.target_angle - *s->coordinate,
                                   s->motor_impulse,
                                   s->limit_impulse,
                                   s->linear_impulse,
                                   region && region->fidelity != world::RegionFidelity::inactive &&
                                       s->enabled};
}
BodyControlResult AuthoredSimulation::control_powered_hinge(world::PersistentId owner,
                                                            const world::JointDrive &d,
                                                            bool enabled) {
    if (!world::valid(d))
        return BodyControlResult::invalid_argument;
    const auto i = state_->joint_lookup.find(owner);
    if (!state_->world || i == state_->joint_lookup.end())
        return BodyControlResult::not_found;
    const auto observed = powered_hinge(owner);
    if (!observed)
        return BodyControlResult::reset_required;
    const auto &j = state_->joints[i->second];
    const auto region = state_->world->region(j.region);
    if (!region)
        return BodyControlResult::reset_required;
    if (region->fidelity == world::RegionFidelity::inactive)
        return BodyControlResult::inactive_region;
    auto h = observed->intent;
    h.drive = d;
    h.enabled = enabled;
    if (!world::valid(h))
        return BodyControlResult::invalid_argument;
    return state_->world->set_powered_hinge(j.entity, h) ? BodyControlResult::applied
                                                         : BodyControlResult::reset_required;
}
} // namespace vice::rigid_body

namespace vice::rigid_body {
std::optional<SwingTwistObservation>
AuthoredSimulation::swing_twist_joint(world::PersistentId owner) const noexcept {
    if (!state_->world || state_->unpublished_step ||
        state_->world->powered_hinge_revision() != state_->joint_revision)
        return {};
    const auto i = state_->swing_lookup.find(owner);
    if (i == state_->swing_lookup.end())
        return {};
    const auto &j = state_->swing_joints[i->second];
    const auto id = state_->world->find(owner);
    const auto h = id ? state_->world->swing_twist_joint(*id) : std::nullopt;
    if (!h || *id != j.entity)
        return {};
    auto config = *h;
    config.configuration.drive = j.expected.configuration.drive;
    config.enabled = j.expected.enabled;
    if (config != j.expected)
        return {};
    const auto a = state_->body_lookup.find(h->first), b = state_->body_lookup.find(h->second);
    if (a == state_->body_lookup.end() || b == state_->body_lookup.end() ||
        !state_->matches(state_->bodies[a->second]) || !state_->matches(state_->bodies[b->second]))
        return {};
    const auto s = state_->domain(j.region)->constraint_state(j.native_id);
    if (!s || !s->relative_orientation)
        return {};
    const auto q = *s->relative_orientation;
    const auto &t = h->configuration.drive.target_orientation;
    double dot = q.x * t[0] + q.y * t[1] + q.z * t[2] + q.w * t[3];
    double norm = 0;
    for (auto v : t)
        norm += double(v) * v;
    const float error =
        static_cast<float>(2 * std::acos(std::clamp(std::abs(dot) / std::sqrt(norm), 0.0, 1.0)));
    const auto region = state_->world->region(j.region);
    return SwingTwistObservation{*h,
                                 q,
                                 error,
                                 s->motor_impulses,
                                 s->limit_impulses,
                                 s->linear_impulse,
                                 region && region->fidelity != world::RegionFidelity::inactive &&
                                     s->enabled};
}
BodyControlResult AuthoredSimulation::control_swing_twist_joint(world::PersistentId owner,
                                                                const world::SwingTwistDrive &drive,
                                                                bool enabled) {
    if (!world::valid(drive))
        return BodyControlResult::invalid_argument;
    if (!state_->world)
        return BodyControlResult::not_found;
    if (!state_->swing_lookup.contains(owner))
        return BodyControlResult::not_found;
    const auto o = swing_twist_joint(owner);
    if (!o)
        return BodyControlResult::reset_required;
    const auto &j = state_->swing_joints[state_->swing_lookup.at(owner)];
    const auto region = state_->world->region(j.region);
    if (!region)
        return BodyControlResult::reset_required;
    if (region->fidelity == world::RegionFidelity::inactive)
        return BodyControlResult::inactive_region;
    auto h = o->intent;
    h.configuration.drive = drive;
    h.enabled = enabled;
    if (!world::valid(h))
        return BodyControlResult::invalid_argument;
    return state_->world->set_swing_twist_joint(j.entity, h) ? BodyControlResult::applied
                                                             : BodyControlResult::reset_required;
}
} // namespace vice::rigid_body
