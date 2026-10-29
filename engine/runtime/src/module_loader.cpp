#include <vice/runtime/module_loader.hpp>
#if defined(VICE_RUNTIME_AUTHORED_PHYSICS)
#include <vice/rigid_body/authored_simulation.hpp>
#endif

#include <vice/core/log.hpp>
#include <vice/platform/dynamic_library.hpp>
#include <vice/runtime/module_api.hpp>

#include <cstdint>
#include <cmath>
#include <filesystem>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

namespace vice::runtime {
namespace {

struct HostBridge {
    GameContext* context = nullptr;
    bool writable = false;
};

// A retained HostApi cannot access a callback's World from another thread or phase.
thread_local HostBridge* active_bridge = nullptr;
struct CallbackScope {
    HostBridge& bridge;
    HostBridge* previous;
    CallbackScope(HostBridge& b, GameContext& context, bool writable) : bridge{b}, previous{active_bridge} {
        bridge.context = &context;
        bridge.writable = writable;
        active_bridge = &bridge;
    }
    ~CallbackScope() { active_bridge = previous; bridge.context = nullptr; bridge.writable = false; }
};
HostBridge* current_bridge(void* value) noexcept {
    return active_bridge == value ? active_bridge : nullptr;
}
world::PersistentId persistent_id(module::EntityId id) noexcept {
    world::PersistentId::Bytes bytes{};
    for (std::size_t i = 0; i < bytes.size(); ++i) bytes[i] = id.bytes[i];
    return world::PersistentId{bytes};
}
module::Result host_read_entity(void* value, module::EntityId id, module::EntitySnapshot* output) noexcept {
    const auto* bridge = current_bridge(value);
    if (!bridge || !bridge->context) return module::Result::wrong_phase;
    if (!output || output->size < sizeof(module::EntitySnapshot) || persistent_id(id).is_nil()) return module::Result::invalid_argument;
    const auto& world = bridge->context->world;
    const auto entity = world.find(persistent_id(id));
    if (!entity) return module::Result::not_found;
    module::EntitySnapshot result;
    result.id = id;
    if (const auto spatial = world.spatial_state(*entity)) {
        result.flags |= 1U;
        result.region = spatial->region.value;
        result.position[0] = spatial->global_position.x;
        result.position[1] = spatial->global_position.y;
        result.position[2] = spatial->global_position.z;
        if (const auto region = world.region(spatial->region)) result.fidelity = static_cast<std::uint32_t>(region->fidelity);
    }
    if (const auto transform = world.authored_transform(*entity)) {
        result.rotation[0] = transform->rotation.x; result.rotation[1] = transform->rotation.y;
        result.rotation[2] = transform->rotation.z; result.rotation[3] = transform->rotation.w;
        result.scale[0] = transform->scale.x; result.scale[1] = transform->scale.y; result.scale[2] = transform->scale.z;
    }
    if (const auto box = world.rigid_box(*entity)) {
        result.flags |= 2U;
        switch (box->motion) {
        case world::RigidMotion::static_body: result.motion = module::Motion::static_body; break;
        case world::RigidMotion::kinematic_body: result.motion = module::Motion::kinematic_body; break;
        case world::RigidMotion::dynamic_body: result.motion = module::Motion::dynamic_body; break;
        }
        result.half_extents = {box->half_extents.x, box->half_extents.y, box->half_extents.z};
        result.linear_velocity = {box->linear_velocity.x, box->linear_velocity.y, box->linear_velocity.z};
        result.angular_velocity = {box->angular_velocity.x, box->angular_velocity.y, box->angular_velocity.z};
    }
    if (const auto c = world.character(*entity)) { result.flags |= 4U; result.linear_velocity = {c->velocity.x,c->velocity.y,c->velocity.z}; }
    *output = result;
    return module::Result::success;
}
module::Result host_set_body_velocity(void* value, module::EntityId id, module::Vector3 linear, module::Vector3 angular) noexcept {
    const auto* bridge = current_bridge(value);
    if (!bridge || !bridge->context || !bridge->writable) return module::Result::wrong_phase;
    if (persistent_id(id).is_nil()) return module::Result::invalid_argument;
    const auto entity = bridge->context->world.find(persistent_id(id));
    if (!entity) return module::Result::not_found;
    if (!bridge->context->world.rigid_box(*entity)) return module::Result::no_body;
#if defined(VICE_RUNTIME_AUTHORED_PHYSICS)
    if (!bridge->context->physics) return module::Result::unavailable;
    using C = rigid_body::BodyControlResult;
    switch (bridge->context->physics->set_body_velocity(persistent_id(id), {linear.x, linear.y, linear.z}, {angular.x, angular.y, angular.z})) {
    case C::applied: return module::Result::success;
    case C::not_found: return module::Result::reset_required;
    case C::invalid_argument: return module::Result::invalid_argument;
    case C::static_body: return module::Result::static_body;
    case C::inactive_region: return module::Result::inactive_region;
    case C::reset_required: return module::Result::reset_required;
    }
#else
    (void)linear; (void)angular;
#endif
    return module::Result::unavailable;
}

module::Result host_read_body_support(void* value, module::EntityId id, module::Vector3 up, float min_up_dot, module::SupportSnapshot* output) noexcept {
    const auto* bridge = current_bridge(value);
    if (!bridge || !bridge->context) return module::Result::wrong_phase;
    const double length = std::hypot(static_cast<double>(up.x), static_cast<double>(up.y), static_cast<double>(up.z));
    if (!output || output->size < sizeof(module::SupportSnapshot) || persistent_id(id).is_nil() ||
        !std::isfinite(length) || length <= 0 || !std::isfinite(min_up_dot) || min_up_dot < -1 || min_up_dot > 1)
        return module::Result::invalid_argument;
    const auto entity = bridge->context->world.find(persistent_id(id));
    if (!entity) return module::Result::not_found;
    if (!bridge->context->world.rigid_box(*entity)) return module::Result::no_body;
#if defined(VICE_RUNTIME_AUTHORED_PHYSICS)
    if (!bridge->context->physics) return module::Result::unavailable;
    const auto support = bridge->context->physics->body_support(persistent_id(id), {up.x, up.y, up.z}, min_up_dot);
    if (!support) return module::Result::reset_required;
    module::SupportSnapshot result;
    result.supported = support->supported ? 1U : 0U;
    for (std::size_t i = 0; i < 16; ++i) result.supporting_entity.bytes[i] = support->supporting_entity.bytes()[i];
    result.point[0] = support->point.x; result.point[1] = support->point.y; result.point[2] = support->point.z;
    result.normal = {support->normal.x, support->normal.y, support->normal.z};
    result.velocity = {support->velocity.x, support->velocity.y, support->velocity.z};
    *output = result;
    return module::Result::success;
#else
    return module::Result::unavailable;
#endif
}

module::Result host_read_distance_link(void* value, module::EntityId id, module::DistanceLinkSnapshot* output) noexcept {
    const auto* bridge = current_bridge(value);
    if (!bridge || !bridge->context) return module::Result::wrong_phase;
    if (!output || output->size < sizeof(module::DistanceLinkSnapshot) || persistent_id(id).is_nil()) return module::Result::invalid_argument;
    const auto entity = bridge->context->world.find(persistent_id(id));
    if (!entity) return module::Result::not_found;
    const auto link = bridge->context->world.distance_link(*entity);
    if (!link) return module::Result::no_link;
    module::DistanceLinkSnapshot result;
    for (std::size_t i = 0; i < 16; ++i) { result.first.bytes[i] = link->first.bytes()[i]; result.second.bytes[i] = link->second.bytes()[i]; }
    result.min_distance = link->min_distance; result.max_distance = link->max_distance;
    result.frequency_hz = link->frequency_hz; result.damping_ratio = link->damping_ratio;
    result.break_threshold = link->break_threshold; result.last_break_impulse = link->last_break_impulse;
    result.broken = link->broken ? 1U : 0U; result.break_step = link->break_step;
    *output = result;
    return module::Result::success;
}

module::Result host_read_dependency(void* value, module::EntityId id, module::DependencySnapshot* output) noexcept {
    const auto* bridge = current_bridge(value);
    if (!bridge || !bridge->context) return module::Result::wrong_phase;
    if (!output || output->size < sizeof(module::DependencySnapshot) || persistent_id(id).is_nil()) return module::Result::invalid_argument;
    const auto entity = bridge->context->world.find(persistent_id(id));
    if (!entity) return module::Result::not_found;
    const auto state = bridge->context->world.dependency_state(*entity);
    if (!state) return module::Result::no_dependency;
    module::DependencySnapshot result;
    for (std::size_t i = 0; i < 16; ++i) {
        result.provider.bytes[i] = state->dependency.provider.bytes()[i];
        result.gate.bytes[i] = state->dependency.gate.bytes()[i];
        result.root_cause.bytes[i] = state->root_cause.bytes()[i];
    }
    result.available = state->available ? 1U : 0U;
    result.reason = static_cast<module::DependencyReason>(state->reason);
    result.root_reason = static_cast<module::DependencyReason>(state->root_reason);
    result.break_impulse = state->break_impulse; result.break_step = state->break_step;
    result.path_length = static_cast<std::uint32_t>(state->path.size());
    *output = result; return module::Result::success;
}

module::Result host_read_character_attachment(void* value,module::EntityId id,module::CharacterAttachmentSnapshot* output) noexcept {
    const auto* bridge=current_bridge(value);if(!bridge || !bridge->context)return module::Result::wrong_phase;
    if(!output || output->size<sizeof(module::CharacterAttachmentSnapshot) || persistent_id(id).is_nil())return module::Result::invalid_argument;
    const auto& world=bridge->context->world;const auto e=world.find(persistent_id(id));if(!e)return module::Result::not_found;
    if(!world.character(*e))return module::Result::no_character;
    module::CharacterAttachmentSnapshot result;
    if(const auto a=world.character_attachment(*e)){result.attached=1;result.offset={a->offset.x,a->offset.y,a->offset.z};for(std::size_t i=0;i<16;++i)result.carrier.bytes[i]=a->carrier.bytes()[i];}
    *output=result;return module::Result::success;
}
#if defined(VICE_RUNTIME_AUTHORED_PHYSICS)
module::Result attachment_result(rigid_body::AttachmentControlResult result) noexcept {
    using R=rigid_body::AttachmentControlResult;
    switch(result){case R::applied:return module::Result::success;case R::not_found:return module::Result::not_found;
    case R::invalid_argument:return module::Result::invalid_argument;case R::inactive_region:return module::Result::inactive_region;
    case R::reset_required:return module::Result::reset_required;case R::out_of_range:return module::Result::out_of_range;
    case R::already_attached:return module::Result::attached;case R::not_attached:return module::Result::no_attachment;case R::blocked:return module::Result::blocked;}
    return module::Result::unavailable;
}
#endif
module::Result host_attach_character(void* value,module::EntityId id,module::EntityId carrier,module::Vector3 offset,float distance) noexcept {
    const auto* bridge=current_bridge(value);if(!bridge||!bridge->context||!bridge->writable)return module::Result::wrong_phase;
    if(persistent_id(id).is_nil()||persistent_id(carrier).is_nil()||!std::isfinite(offset.x)||!std::isfinite(offset.y)||!std::isfinite(offset.z)||std::hypot(offset.x,offset.y,offset.z)>100||!std::isfinite(distance)||distance<0||distance>100)return module::Result::invalid_argument;
#if defined(VICE_RUNTIME_AUTHORED_PHYSICS)
    if(bridge->context->physics)return attachment_result(bridge->context->physics->attach_character(persistent_id(id),persistent_id(carrier),{offset.x,offset.y,offset.z},distance));
#endif
    return module::Result::unavailable;
}
module::Result host_detach_character(void* value,module::EntityId id,module::Vector3 offset,float speed,float drop) noexcept {
    const auto* bridge=current_bridge(value);if(!bridge||!bridge->context||!bridge->writable)return module::Result::wrong_phase;
    if(persistent_id(id).is_nil()||!std::isfinite(offset.x)||!std::isfinite(offset.y)||!std::isfinite(offset.z)||std::hypot(offset.x,offset.y,offset.z)>100||!std::isfinite(speed)||!std::isfinite(drop)||speed<0||speed>100||drop<0||drop>5)return module::Result::invalid_argument;
#if defined(VICE_RUNTIME_AUTHORED_PHYSICS)
    if(bridge->context->physics)return attachment_result(bridge->context->physics->detach_character(persistent_id(id),{offset.x,offset.y,offset.z},speed,drop));
#endif
    return module::Result::unavailable;
}
module::Result host_read_camera_pose(void* value,module::EntityId id,module::CameraPose* output) noexcept {
    const auto* bridge=current_bridge(value);if(!bridge||!bridge->context)return module::Result::wrong_phase;
    if(!output||output->size<sizeof(module::CameraPose)||persistent_id(id).is_nil())return module::Result::invalid_argument;
    const auto& world=bridge->context->world;const auto e=world.find(persistent_id(id));if(!e)return module::Result::not_found;
    if(!world.camera(*e))return module::Result::no_camera;
    const auto spatial=world.spatial_state(*e);if(!spatial)return module::Result::invalid_argument;
    const auto rotation=world.authored_transform(*e).value_or(world::AuthoredTransform{}).rotation;
    module::CameraPose result;result.position[0]=spatial->global_position.x;result.position[1]=spatial->global_position.y;result.position[2]=spatial->global_position.z;
    result.rotation[0]=rotation.x;result.rotation[1]=rotation.y;result.rotation[2]=rotation.z;result.rotation[3]=rotation.w;*output=result;return module::Result::success;
}
module::Result host_set_camera_pose(void* value,module::EntityId id,const module::CameraPose* pose) noexcept {
    const auto* bridge=current_bridge(value);if(!bridge||!bridge->context||!bridge->writable)return module::Result::wrong_phase;
    if(!pose||pose->size<sizeof(module::CameraPose)||persistent_id(id).is_nil())return module::Result::invalid_argument;
    auto& world=bridge->context->world;const auto e=world.find(persistent_id(id));if(!e)return module::Result::not_found;
    if(!world.camera(*e))return module::Result::no_camera;
    const auto spatial=world.spatial_state(*e);if(!spatial||world.rigid_box(*e)||world.character(*e))return module::Result::invalid_argument;
    auto transform=world.authored_transform(*e).value_or(world::AuthoredTransform{});
    transform.rotation={pose->rotation[0],pose->rotation[1],pose->rotation[2],pose->rotation[3]};
    const world::GlobalPosition position{pose->position[0],pose->position[1],pose->position[2]};
    if(!world::finite(position)||!world::valid(transform))return module::Result::invalid_argument;
    // Both records are validated before publishing; camera entities have no physical representation.
    if(!world.set_authored_transform(*e,transform)||!world.set_spatial_state(*e,{position,spatial->region}))return module::Result::invalid_argument;
    return module::Result::success;
}

module::Result host_read_vehicle(void* value,module::EntityId id,module::VehicleSnapshot* output) noexcept {
    const auto* bridge=current_bridge(value);
    if(!bridge || !bridge->context)return module::Result::wrong_phase;
    if(!output || output->size<sizeof(module::VehicleSnapshot) || persistent_id(id).is_nil())return module::Result::invalid_argument;
    const auto& world=bridge->context->world;
    const auto entity=world.find(persistent_id(id));
    if(!entity)return module::Result::not_found;
    if(!world.vehicle(*entity))return module::Result::no_vehicle;
#if defined(VICE_RUNTIME_AUTHORED_PHYSICS)
    if(!bridge->context->physics)return module::Result::unavailable;
    const auto observed=bridge->context->physics->vehicle(persistent_id(id));
    if(!observed)return module::Result::reset_required;
    module::VehicleSnapshot result;
    result.throttle=observed->state.throttle;result.steering=observed->state.steering;result.brake=observed->state.brake;
    result.distance=observed->state.distance;result.forward_speed=observed->forward_speed;
    for(std::size_t i=0;i<4;++i) {
        const auto& w=observed->wheels[i];auto& o=result.wheels[i];
        o.grounded=w.grounded?1U:0U;
        for(std::size_t j=0;j<16;++j){o.supporting_entity.bytes[j]=w.supporting_entity.bytes()[j];o.material.bytes[j]=w.material.bytes()[j];}
        o.point[0]=w.point.x;o.point[1]=w.point.y;o.point[2]=w.point.z;o.normal={w.normal.x,w.normal.y,w.normal.z};
        o.suspension_length=w.suspension_length;o.normal_force=w.normal_force;o.longitudinal_force=w.longitudinal_force;o.lateral_force=w.lateral_force;
    }
    *output=result;return module::Result::success;
#else
    return module::Result::unavailable;
#endif
}
module::Result host_control_vehicle(void* value,module::EntityId id,float throttle,float steering,float brake) noexcept {
    const auto* bridge=current_bridge(value);
    if(!bridge || !bridge->context || !bridge->writable)return module::Result::wrong_phase;
    if(persistent_id(id).is_nil() || !std::isfinite(throttle) || !std::isfinite(steering) || !std::isfinite(brake) ||
        std::abs(throttle)>1 || std::abs(steering)>1 || brake<0 || brake>1)return module::Result::invalid_argument;
    const auto entity=bridge->context->world.find(persistent_id(id));
    if(!entity)return module::Result::not_found;
    if(!bridge->context->world.vehicle(*entity))return module::Result::no_vehicle;
#if defined(VICE_RUNTIME_AUTHORED_PHYSICS)
    if(!bridge->context->physics)return module::Result::unavailable;
    using C=rigid_body::VehicleControlResult;
    switch(bridge->context->physics->control_vehicle(persistent_id(id),throttle,steering,brake)) {
    case C::applied:return module::Result::success;
    case C::not_found:case C::reset_required:return module::Result::reset_required;
    case C::invalid_argument:return module::Result::invalid_argument;
    case C::inactive_region:return module::Result::inactive_region;
    }
#endif
    return module::Result::unavailable;
}

module::Result host_read_character(void* value, module::EntityId id, module::CharacterSnapshot* output) noexcept {
    const auto* bridge = current_bridge(value);
    if (!bridge || !bridge->context) return module::Result::wrong_phase;
    if (!output || output->size < sizeof(module::CharacterSnapshot) || persistent_id(id).is_nil()) return module::Result::invalid_argument;
    const auto& world = bridge->context->world;
    const auto entity = world.find(persistent_id(id));
    if (!entity) return module::Result::not_found;
    const auto c = world.character(*entity);
    if (!c) return module::Result::no_character;
#if defined(VICE_RUNTIME_AUTHORED_PHYSICS)
    if (!bridge->context->physics) return module::Result::unavailable;
    const auto observed = bridge->context->physics->character(persistent_id(id));
    if (!observed) return module::Result::reset_required;
    module::CharacterSnapshot result;
    const auto spatial = world.spatial_state(*entity);
    if (!spatial) return module::Result::reset_required;
    result.position[0]=spatial->global_position.x; result.position[1]=spatial->global_position.y; result.position[2]=spatial->global_position.z;
    result.height=c->height; result.radius=c->radius; result.mass=c->mass; result.max_strength=c->max_strength;
    result.max_slope_angle=c->max_slope_angle; result.step_height=c->step_height; result.stick_to_floor_distance=c->stick_to_floor_distance;
    const auto vector = [](math::Vec3 v) { return module::Vector3{v.x,v.y,v.z}; };
    result.velocity=vector(observed->state.velocity); result.horizontal_velocity=vector(observed->state.horizontal_velocity);
    result.ground_normal=vector(observed->state.ground_normal); result.ground_velocity=vector(observed->state.ground_velocity);
    result.pending_jump_speed=observed->state.pending_jump_speed.value_or(0);
    result.ground_state=static_cast<module::CharacterGroundState>(observed->state.ground_state);
    for (std::size_t i=0;i<16;++i) result.supporting_entity.bytes[i]=observed->supporting_entity.bytes()[i];
    *output=result; return module::Result::success;
#else
    return module::Result::unavailable;
#endif
}
module::Result host_control_character(void* value, module::EntityId id, module::Vector3 horizontal, float jump_speed) noexcept {
    const auto* bridge = current_bridge(value);
    if (!bridge || !bridge->context || !bridge->writable) return module::Result::wrong_phase;
    if (persistent_id(id).is_nil() || !std::isfinite(horizontal.x) || horizontal.y != 0 || !std::isfinite(horizontal.z) || !std::isfinite(jump_speed) || jump_speed < 0) return module::Result::invalid_argument;
    const auto entity = bridge->context->world.find(persistent_id(id));
    if (!entity) return module::Result::not_found;
    if (!bridge->context->world.character(*entity)) return module::Result::no_character;
#if defined(VICE_RUNTIME_AUTHORED_PHYSICS)
    if (!bridge->context->physics) return module::Result::unavailable;
    using C = rigid_body::CharacterControlResult;
    switch (bridge->context->physics->control_character(persistent_id(id),{horizontal.x,horizontal.y,horizontal.z},jump_speed)) {
    case C::applied: return module::Result::success;
    case C::not_found: case C::reset_required: return module::Result::reset_required;
    case C::invalid_argument: return module::Result::invalid_argument;
    case C::inactive_region: return module::Result::inactive_region;
    case C::not_grounded: return module::Result::not_grounded;
    case C::attached:return module::Result::attached;
    }
#endif
    return module::Result::unavailable;
}

module::EntityId module_id(world::PersistentId id) noexcept { module::EntityId result;for(std::size_t i=0;i<16;++i)result.bytes[i]=id.bytes()[i];return result; }
module::Result host_read_causal_history(void* value,module::CausalHistorySnapshot* output) noexcept {
    const auto* b=current_bridge(value);if(!b || !b->context)return module::Result::wrong_phase;
    if(!output || output->size<sizeof(module::CausalHistorySnapshot))return module::Result::invalid_argument;
    const auto info=b->context->world.causal_info();module::CausalHistorySnapshot result;
    result.history_id=module_id(info.id);result.capture_id=module_id(info.capture_id);result.capturing=info.capturing?1U:0U;
    result.first_sequence=info.first_sequence;result.next_sequence=info.next_sequence;result.retained_count=static_cast<std::uint32_t>(info.retained_count);result.capacity=world::causal_history_capacity;
    *output=result;return module::Result::success;
}
module::Result host_read_causal_event(void* value,module::EntityId history,std::uint64_t sequence,module::CausalEventSnapshot* output) noexcept {
    const auto* b=current_bridge(value);if(!b || !b->context)return module::Result::wrong_phase;
    if(!output || output->size<sizeof(module::CausalEventSnapshot) || sequence==0 || persistent_id(history).is_nil())return module::Result::invalid_argument;
    const auto info=b->context->world.causal_info();
    if(info.id!=persistent_id(history))return module::Result::history_mismatch;
    if(sequence<info.first_sequence)return module::Result::event_pruned;
    const auto e=b->context->world.causal_event(sequence);if(!e)return module::Result::not_found;
    module::CausalEventSnapshot result;
    result.kind=static_cast<module::CausalKind>(e->kind);result.origin=static_cast<module::CausalOrigin>(e->origin);result.value=e->value?1U:0U;
    result.quantity_kind=static_cast<module::CausalQuantity>(e->quantity_kind);result.reason=static_cast<module::DependencyReason>(e->reason);result.root_reason=static_cast<module::DependencyReason>(e->root_reason);
    result.parent_retained=e->parent_sequence>=info.first_sequence && e->parent_sequence?1U:0U;
    result.sequence=e->sequence;result.parent_sequence=e->parent_sequence;result.solver_step=e->solver_step;result.region=e->region.value;result.quantity=e->quantity;
    result.id=module_id(e->id);result.parent_id=module_id(e->parent_id);result.capture_id=module_id(e->capture_id);result.subject=module_id(e->subject);
    result.first=module_id(e->first);result.second=module_id(e->second);result.root_entity=module_id(e->root_entity);*output=result;return module::Result::success;
}
module::Result host_read_material(void* value,module::EntityId id,module::MaterialSnapshot* output) noexcept {
    const auto* b=current_bridge(value);if(!b || !b->context)return module::Result::wrong_phase;
    if(!output || output->size<sizeof(module::MaterialSnapshot) || persistent_id(id).is_nil())return module::Result::invalid_argument;
    const auto entity=b->context->world.find(persistent_id(id));if(!entity)return module::Result::not_found;
    const auto surface=b->context->world.material_surface(*entity);if(!surface)return module::Result::unavailable;
    module::MaterialSnapshot result;
    result.id=module_id(surface->id);result.render_material=module_id(surface->render_material.value);
    result.density=surface->density;result.friction=surface->friction;result.restitution=surface->restitution;result.volume=surface->volume;result.mass=surface->mass;
#if defined(VICE_RUNTIME_AUTHORED_PHYSICS)
    if(b->context->physics && b->context->world.rigid_box(*entity)) {
        const auto body=b->context->physics->body(persistent_id(id));if(!body)return module::Result::reset_required;
        result.has_native_mass=1;result.native_mass=body->mass;
    }
#endif
    *output=result;return module::Result::success;
}
module::Result host_read_causal_materials(void* value,module::EntityId history,std::uint64_t sequence,module::CausalMaterialSnapshot* output) noexcept {
    const auto* b=current_bridge(value);if(!b || !b->context)return module::Result::wrong_phase;
    if(!output || output->size<sizeof(module::CausalMaterialSnapshot))return module::Result::invalid_argument;
    module::CausalEventSnapshot check;
    const auto state=host_read_causal_event(value,history,sequence,&check);if(state!=module::Result::success)return state;
    const auto e=b->context->world.causal_event(sequence);
    module::CausalMaterialSnapshot result;result.event_id=module_id(e->id);
    result.first_material=module_id(e->first_material);result.second_material=module_id(e->second_material);*output=result;return module::Result::success;
}
module::Result host_set_dependency_enabled(void* value,module::EntityId id,std::uint32_t enabled) noexcept {
    const auto* b=current_bridge(value);if(!b || !b->context || !b->writable)return module::Result::wrong_phase;
    if(persistent_id(id).is_nil() || enabled>1U)return module::Result::invalid_argument;
    const auto e=b->context->world.find(persistent_id(id));if(!e)return module::Result::not_found;
    if(!b->context->world.dependency(*e))return module::Result::no_dependency;
    return b->context->world.set_dependency_enabled(*e,enabled!=0)?module::Result::success:module::Result::sequence_exhausted;
}

[[nodiscard]] std::size_t host_entity_count(void* const value) noexcept {
    const auto* const bridge = current_bridge(value);
    return bridge != nullptr && bridge->context != nullptr
        ? bridge->context->world.entity_count()
        : 0U;
}

[[nodiscard]] std::size_t host_resource_count(void* const value) noexcept {
    const auto* const bridge = current_bridge(value);
    return bridge != nullptr && bridge->context != nullptr
        ? bridge->context->world.resource_count()
        : 0U;
}

void host_log(
    void*,
    const module::LogLevel level,
    const char* const message
) noexcept {
    core::LogLevel engine_level = core::LogLevel::info;
    switch (level) {
        case module::LogLevel::debug: engine_level = core::LogLevel::debug; break;
        case module::LogLevel::info: engine_level = core::LogLevel::info; break;
        case module::LogLevel::warning: engine_level = core::LogLevel::warning; break;
        case module::LogLevel::error: engine_level = core::LogLevel::error; break;
    }
    core::log(engine_level, message != nullptr ? message : "");
}

module::Key module_key(platform::Key key) noexcept {
    switch (key) {
    case platform::Key::unknown: return module::Key::unknown;
    case platform::Key::escape: return module::Key::escape;
    case platform::Key::enter: return module::Key::enter;
    case platform::Key::tab: return module::Key::tab;
    case platform::Key::backspace: return module::Key::backspace;
    case platform::Key::space: return module::Key::space;
    case platform::Key::left: return module::Key::left;
    case platform::Key::right: return module::Key::right;
    case platform::Key::up: return module::Key::up;
    case platform::Key::down: return module::Key::down;
    case platform::Key::home: return module::Key::home;
    case platform::Key::end: return module::Key::end;
    case platform::Key::page_up: return module::Key::page_up;
    case platform::Key::page_down: return module::Key::page_down;
    case platform::Key::insert: return module::Key::insert;
    case platform::Key::delete_key: return module::Key::delete_key;
    case platform::Key::a: return module::Key::a;
    case platform::Key::b: return module::Key::b;
    case platform::Key::c: return module::Key::c;
    case platform::Key::d: return module::Key::d;
    case platform::Key::e: return module::Key::e;
    case platform::Key::f: return module::Key::f;
    case platform::Key::g: return module::Key::g;
    case platform::Key::h: return module::Key::h;
    case platform::Key::i: return module::Key::i;
    case platform::Key::j: return module::Key::j;
    case platform::Key::k: return module::Key::k;
    case platform::Key::l: return module::Key::l;
    case platform::Key::m: return module::Key::m;
    case platform::Key::n: return module::Key::n;
    case platform::Key::o: return module::Key::o;
    case platform::Key::p: return module::Key::p;
    case platform::Key::q: return module::Key::q;
    case platform::Key::r: return module::Key::r;
    case platform::Key::s: return module::Key::s;
    case platform::Key::t: return module::Key::t;
    case platform::Key::u: return module::Key::u;
    case platform::Key::v: return module::Key::v;
    case platform::Key::w: return module::Key::w;
    case platform::Key::x: return module::Key::x;
    case platform::Key::y: return module::Key::y;
    case platform::Key::z: return module::Key::z;
    case platform::Key::digit_0: return module::Key::digit_0;
    case platform::Key::digit_1: return module::Key::digit_1;
    case platform::Key::digit_2: return module::Key::digit_2;
    case platform::Key::digit_3: return module::Key::digit_3;
    case platform::Key::digit_4: return module::Key::digit_4;
    case platform::Key::digit_5: return module::Key::digit_5;
    case platform::Key::digit_6: return module::Key::digit_6;
    case platform::Key::digit_7: return module::Key::digit_7;
    case platform::Key::digit_8: return module::Key::digit_8;
    case platform::Key::digit_9: return module::Key::digit_9;
    case platform::Key::f1: return module::Key::f1;
    case platform::Key::f2: return module::Key::f2;
    case platform::Key::f3: return module::Key::f3;
    case platform::Key::f4: return module::Key::f4;
    case platform::Key::f5: return module::Key::f5;
    case platform::Key::f6: return module::Key::f6;
    case platform::Key::f7: return module::Key::f7;
    case platform::Key::f8: return module::Key::f8;
    case platform::Key::f9: return module::Key::f9;
    case platform::Key::f10: return module::Key::f10;
    case platform::Key::f11: return module::Key::f11;
    case platform::Key::f12: return module::Key::f12;
    }
    return module::Key::unknown;
}

[[nodiscard]] module::Event translate_event(const platform::Event& event) noexcept {
    module::Event translated;
    std::visit(
        [&](const auto& payload) {
            using Payload = std::decay_t<decltype(payload)>;
            if constexpr (std::is_same_v<Payload, platform::QuitRequested>) {
                translated.kind = module::EventKind::quit_requested;
            } else if constexpr (std::is_same_v<Payload, platform::WindowCloseRequested>) {
                translated.kind = module::EventKind::window_close_requested;
                translated.window = payload.window;
            } else if constexpr (std::is_same_v<Payload, platform::WindowMetricsChanged>) {
                translated.kind = module::EventKind::window_metrics_changed;
                translated.window = payload.window;
                translated.width = payload.metrics.width;
                translated.height = payload.metrics.height;
                translated.drawable_width = payload.metrics.drawable_width;
                translated.drawable_height = payload.metrics.drawable_height;
                translated.scale_factor = payload.metrics.scale_factor;
            } else if constexpr (std::is_same_v<Payload, platform::WindowFocusChanged>) {
                translated.kind = module::EventKind::window_focus_changed;
                translated.window = payload.window;
                translated.flags = payload.focused ? 1U : 0U;
            } else if constexpr (std::is_same_v<Payload, platform::KeyEvent>) {
                translated.kind = module::EventKind::key;
                translated.window = payload.window;
                translated.code = static_cast<std::uint32_t>(module_key(payload.key));
                translated.modifiers = payload.modifiers;
                translated.flags = (payload.pressed ? 1U : 0U) | (payload.repeat ? 2U : 0U);
            } else if constexpr (std::is_same_v<Payload, platform::TextInputEvent>) {
                translated.kind = module::EventKind::text_input;
                translated.window = payload.window;
                translated.code = static_cast<std::uint32_t>(payload.codepoint);
            } else if constexpr (std::is_same_v<Payload, platform::MouseMoveEvent>) {
                translated.kind = module::EventKind::mouse_move;
                translated.window = payload.window;
                translated.x = payload.x;
                translated.y = payload.y;
                translated.delta_x = payload.delta_x;
                translated.delta_y = payload.delta_y;
            } else if constexpr (std::is_same_v<Payload, platform::MouseButtonEvent>) {
                translated.kind = module::EventKind::mouse_button;
                translated.window = payload.window;
                translated.code = static_cast<std::uint32_t>(payload.button);
                translated.flags = payload.pressed ? 1U : 0U;
                translated.x = payload.x;
                translated.y = payload.y;
            } else if constexpr (std::is_same_v<Payload, platform::MouseWheelEvent>) {
                translated.kind = module::EventKind::mouse_wheel;
                translated.window = payload.window;
                translated.code = static_cast<std::uint32_t>(payload.unit);
                translated.delta_x = payload.delta_x;
                translated.delta_y = payload.delta_y;
            }
        },
        event.payload
    );
    return translated;
}

class DynamicGameModule final : public GameModule {
public:
    explicit DynamicGameModule(std::unique_ptr<platform::DynamicLibrary> library) noexcept
        : library_{std::move(library)} {
        host_.context = &bridge_;
        host_.world_entity_count = &host_entity_count;
        host_.world_resource_count = &host_resource_count;
        host_.log = &host_log;
        host_.read_entity = &host_read_entity;
        host_.set_body_velocity = &host_set_body_velocity;
        host_.read_body_support = &host_read_body_support;
        host_.read_distance_link = &host_read_distance_link;
        host_.read_dependency = &host_read_dependency;
        host_.read_character = &host_read_character;
        host_.control_character = &host_control_character;
        host_.read_causal_history = &host_read_causal_history;
        host_.read_causal_event = &host_read_causal_event;
        host_.set_dependency_enabled = &host_set_dependency_enabled;
        host_.read_material = &host_read_material;
        host_.read_causal_materials = &host_read_causal_materials;
        host_.read_vehicle=&host_read_vehicle;
        host_.control_vehicle=&host_control_vehicle;
        host_.read_character_attachment=&host_read_character_attachment;
        host_.attach_character=&host_attach_character;host_.detach_character=&host_detach_character;
        host_.read_camera_pose=&host_read_camera_pose;host_.set_camera_pose=&host_set_camera_pose;
    }

    ~DynamicGameModule() override {
        if (api_.state != nullptr && api_.destroy != nullptr) {
            api_.destroy(api_.state);
            api_.state = nullptr;
        }
    }

    [[nodiscard]] bool initialize(
        const module::LoadFunction load,
        std::string& diagnostics
    ) noexcept {
        if (load == nullptr) {
            diagnostics = "Game module load entrypoint is null.";
            return false;
        }

        module::ModuleApi loaded_api{};
        if (!load(module::abi_version, &host_, &loaded_api)) {
            diagnostics = "Game module rejected VICE module ABI version " +
                std::to_string(module::abi_version) + ".";
            return false;
        }

        const bool valid = loaded_api.abi_version == module::abi_version &&
            loaded_api.size >= sizeof(module::ModuleApi) &&
            loaded_api.state != nullptr &&
            loaded_api.start != nullptr &&
            loaded_api.event != nullptr &&
            loaded_api.update != nullptr &&
            loaded_api.frame != nullptr &&
            loaded_api.stop != nullptr &&
            loaded_api.destroy != nullptr;
        if (!valid) {
            if (loaded_api.state != nullptr && loaded_api.destroy != nullptr) {
                loaded_api.destroy(loaded_api.state);
            }
            diagnostics = "Game module returned an incomplete or incompatible module API.";
            return false;
        }

        api_ = loaded_api;
        diagnostics.clear();
        return true;
    }

    bool on_start(GameContext& context) override {
        CallbackScope scope{bridge_, context, true};
        const bool result = api_.start(api_.state, &host_);
        return result;
    }

    void on_event(GameContext& context, const platform::Event& event) override {
        CallbackScope scope{bridge_, context, true};
        const module::Event translated = translate_event(event);
        api_.event(api_.state, &host_, &translated);
    }

    void on_update(GameContext& context, const double delta_seconds) override {
        CallbackScope scope{bridge_, context, true};
        api_.update(api_.state, &host_, delta_seconds);
    }

    void on_frame(GameContext& context, render::Color& clear_color) override {
        CallbackScope scope{bridge_, context, false};
        module::Frame frame;
        frame.clear_color = module::Color{
            .red = clear_color.red,
            .green = clear_color.green,
            .blue = clear_color.blue,
            .alpha = clear_color.alpha,
        };
        api_.frame(api_.state, &host_, &frame);
        clear_color = render::Color{
            .red = frame.clear_color.red,
            .green = frame.clear_color.green,
            .blue = frame.clear_color.blue,
            .alpha = frame.clear_color.alpha,
        };
    }

    void on_stop(GameContext& context) override {
        CallbackScope scope{bridge_, context, false};
        api_.stop(api_.state, &host_);
    }

private:
    std::unique_ptr<platform::DynamicLibrary> library_;
    module::ModuleApi api_{};
    HostBridge bridge_{};
    module::HostApi host_{};
};

} // namespace

std::filesystem::path staged_game_module_path(
    const std::filesystem::path& executable,
    const std::string_view game_target
) {
    if (executable.empty() || game_target.empty()) return {};
    return executable.parent_path() /
        "modules" /
        (std::string{game_target} + std::string{platform::dynamic_library_extension()});
}

GameModuleLoadResult load_game_module(const std::filesystem::path& path) {
    GameModuleLoadResult result;
    if (path.empty()) {
        result.diagnostics = "Game module path is empty.";
        return result;
    }

    std::string diagnostics;
    auto library = platform::DynamicLibrary::load(path, &diagnostics);
    if (!library) {
        result.diagnostics = "Could not load game module `" + path.generic_string() + "`: " + diagnostics;
        return result;
    }

    const std::uintptr_t address = library->symbol(module::entrypoint_name, &diagnostics);
    if (address == 0U) {
        result.diagnostics = "Game module `" + path.generic_string() + "` is missing `" +
            std::string{module::entrypoint_name} + "`: " + diagnostics;
        return result;
    }

    const auto load = reinterpret_cast<module::LoadFunction>(address);
    auto module_instance = std::make_unique<DynamicGameModule>(std::move(library));
    if (!module_instance->initialize(load, diagnostics)) {
        result.diagnostics = "Could not initialize game module `" + path.generic_string() + "`: " + diagnostics;
        return result;
    }

    result.module = std::move(module_instance);
    return result;
}

} // namespace vice::runtime
