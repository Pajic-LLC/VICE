#include <vice/runtime/module_loader.hpp>
#include <vice/presentation/system.hpp>
#include <algorithm>
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
module::EntityId component_id(world::PersistentId p) noexcept {
    module::EntityId out;
    std::copy(p.bytes().begin(), p.bytes().end(), out.bytes);
    return out;
}
template <std::size_t N> std::optional<std::string> component_string(const char (&data)[N]) {
    const auto end = std::find(std::begin(data), std::end(data), '\0');
    if (end == std::end(data))
        return {};
    return std::string{std::begin(data), end};
}
static_assert(module::component_field_capacity == world::max_component_fields);
module::Result host_register_component_schema(void *value,
                                              const module::ComponentSchema *input) noexcept {
    const auto *b = current_bridge(value);
    if (!b || !b->context || !b->writable)
        return module::Result::wrong_phase;
    if (!input || input->size < sizeof(*input) ||
        input->field_count > module::component_field_capacity)
        return module::Result::invalid_argument;
    const auto name = component_string(input->name);
    if (!name)
        return module::Result::invalid_argument;
    world::ComponentTypeDesc schema{{persistent_id(input->type)}, *name, input->version};
    for (std::uint32_t i = 0; i < input->field_count; ++i) {
        const auto &f = input->fields[i];
        const auto field = component_string(f.name);
        if (!field)
            return module::Result::invalid_argument;
        schema.fields.push_back({f.id, static_cast<world::ComponentValueKind>(f.kind), *field});
    }
    if (!world::valid_component_type(schema))
        return module::Result::invalid_argument;
    std::sort(schema.fields.begin(), schema.fields.end(),
              [](const auto &a, const auto &c) { return a.id < c.id; });
    auto &w = b->context->world;
    if (const auto existing = w.component_type(schema.id))
        return *existing == schema ? module::Result::success : module::Result::schema_conflict;
    return w.register_component_type(std::move(schema)) ? module::Result::success
                                                        : module::Result::schema_conflict;
}
module::Result host_read_component_schema(void *value, module::EntityId type,
                                          module::ComponentSchema *output) noexcept {
    const auto *b = current_bridge(value);
    if (!b || !b->context)
        return module::Result::wrong_phase;
    if (!output || output->size < sizeof(*output) || persistent_id(type).is_nil())
        return module::Result::invalid_argument;
    const auto schema = b->context->world.component_type({persistent_id(type)});
    if (!schema)
        return module::Result::not_found;
    if (schema->name.size() > 128)
        return module::Result::invalid_argument;
    module::ComponentSchema result;
    result.type = type;
    result.version = schema->version;
    result.field_count = static_cast<std::uint32_t>(schema->fields.size());
    std::copy(schema->name.begin(), schema->name.end(), result.name);
    for (std::size_t i = 0; i < schema->fields.size(); ++i) {
        const auto &f = schema->fields[i];
        auto &out = result.fields[i];
        out.id = f.id;
        out.kind = static_cast<module::ComponentKind>(f.kind);
        std::copy(f.name.begin(), f.name.end(), out.name);
    }
    *output = result;
    return module::Result::success;
}
module::ComponentData encode_component_data(const world::ComponentPayload &data) {
    module::ComponentData result;
    result.type = component_id(data.type.value);
    result.version = data.version;
    result.field_count = static_cast<std::uint32_t>(data.fields.size());
    for (std::size_t i = 0; i < data.fields.size(); ++i) {
        const auto &v = data.fields[i];
        auto &out = result.fields[i];
        out.id = v.id;
        out.kind = static_cast<module::ComponentKind>(world::component_value_kind(v.value));
        switch (out.kind) {
        case module::ComponentKind::boolean:
            out.boolean = std::get<bool>(v.value) ? 1U : 0U;
            break;
        case module::ComponentKind::integer:
            out.integer = std::get<std::int64_t>(v.value);
            break;
        case module::ComponentKind::real:
            out.real = std::get<double>(v.value);
            break;
        case module::ComponentKind::text: {
            const auto &s = std::get<std::string>(v.value);
            std::copy(s.begin(), s.end(), out.text);
            break;
        }
        case module::ComponentKind::identifier:
            out.identifier = component_id(std::get<world::PersistentId>(v.value));
            break;
        }
    }
    return result;
}
std::optional<world::ComponentPayload> decode_component_data(const module::ComponentData *input) {
    if (!input || input->size < sizeof(*input) ||
        input->field_count > module::component_field_capacity)
        return {};
    world::ComponentPayload data{{persistent_id(input->type)}, input->version, {}};
    for (std::uint32_t i = 0; i < input->field_count; ++i) {
        const auto &in = input->fields[i];
        world::ComponentFieldValue v;
        v.id = in.id;
        switch (in.kind) {
        case module::ComponentKind::boolean:
            if (in.boolean > 1)
                return std::nullopt;
            v.value = in.boolean != 0;
            break;
        case module::ComponentKind::integer:
            v.value = in.integer;
            break;
        case module::ComponentKind::real:
            v.value = in.real;
            break;
        case module::ComponentKind::text: {
            const auto s = component_string(in.text);
            if (!s)
                return std::nullopt;
            v.value = *s;
            break;
        }
        case module::ComponentKind::identifier:
            v.value = persistent_id(in.identifier);
            break;
        default:
            return std::nullopt;
        }
        data.fields.push_back(std::move(v));
    }
    return data;
}
module::Result host_read_component(void *value, module::EntityId owner, module::EntityId type,
                                   module::ComponentData *output) noexcept {
    const auto *b = current_bridge(value);
    if (!b || !b->context)
        return module::Result::wrong_phase;
    if (!output || output->size < sizeof(*output) || persistent_id(owner).is_nil() ||
        persistent_id(type).is_nil())
        return module::Result::invalid_argument;
    const auto &w = b->context->world;
    const auto e = w.find(persistent_id(owner));
    if (!e)
        return module::Result::not_found;
    const auto data = w.component(*e, {persistent_id(type)});
    if (!data)
        return module::Result::no_component;
    *output = encode_component_data(*data);
    return module::Result::success;
}
module::Result host_set_component(void *value, module::EntityId owner,
                                  const module::ComponentData *input) noexcept {
    const auto *b = current_bridge(value);
    if (!b || !b->context || !b->writable)
        return module::Result::wrong_phase;
    if (!input || input->size < sizeof(*input) ||
        input->field_count > module::component_field_capacity || persistent_id(owner).is_nil())
        return module::Result::invalid_argument;
    auto &w = b->context->world;
    const auto e = w.find(persistent_id(owner));
    if (!e)
        return module::Result::not_found;
    const auto data = decode_component_data(input);
    if (!data)
        return module::Result::invalid_argument;
    const auto schema = w.component_type(data->type);
    if (!schema)
        return module::Result::not_found;
    if (schema->version != data->version)
        return module::Result::schema_conflict;
    return w.set_component(*e, *data) ? module::Result::success : module::Result::invalid_argument;
}
module::Result host_remove_component(void *value, module::EntityId owner,
                                     module::EntityId type) noexcept {
    const auto *b = current_bridge(value);
    if (!b || !b->context || !b->writable)
        return module::Result::wrong_phase;
    if (persistent_id(owner).is_nil() || persistent_id(type).is_nil())
        return module::Result::invalid_argument;
    auto &w = b->context->world;
    const auto e = w.find(persistent_id(owner));
    if (!e)
        return module::Result::not_found;
    return w.remove_component(*e, {persistent_id(type)}) ? module::Result::success
                                                         : module::Result::no_component;
}
world::RelationKey relation_key(module::RelationKey key) {
    return {persistent_id(key.subject), persistent_id(key.kind), persistent_id(key.object)};
}
module::RelationKey relation_key(world::RelationKey key) {
    return {component_id(key.subject), component_id(key.kind), component_id(key.object)};
}
module::Result host_read_relation(void *value, module::RelationKey key,
                                  module::RelationInfo *output) noexcept {
    const auto *b = current_bridge(value);
    if (!b || !b->context)
        return module::Result::wrong_phase;
    const auto k = relation_key(key);
    if (!output || output->size < sizeof(*output) || !k.valid())
        return module::Result::invalid_argument;
    const auto &w = b->context->world;
    if (!w.has_relation(k))
        return module::Result::no_relation;
    module::RelationInfo result;
    result.key = key;
    result.revision = w.relation_revision();
    result.component_count = static_cast<std::uint32_t>(w.relation_components(k).size());
    *output = result;
    return module::Result::success;
}
module::Result host_read_relations(void *value, module::EntityId endpoint,
                                   module::RelationDirection direction, module::EntityId kind,
                                   module::RelationPage *output) noexcept {
    const auto *b = current_bridge(value);
    if (!b || !b->context)
        return module::Result::wrong_phase;
    const auto id = persistent_id(endpoint);
    const auto d = static_cast<std::uint32_t>(direction);
    if (!output || output->size < sizeof(*output) || id.is_nil() || d < 1 || d > 3)
        return module::Result::invalid_argument;
    const auto &w = b->context->world;
    if (!w.find(id))
        return module::Result::not_found;
    if (output->revision && output->revision != w.relation_revision())
        return module::Result::reset_required;
    if (output->cursor_valid > 1)
        return module::Result::invalid_argument;
    const auto page = w.relation_page(
        id, static_cast<world::RelationDirection>(direction), persistent_id(kind),
        output->cursor_valid ? std::optional{relation_key(output->after)} : std::nullopt,
        module::relation_page_capacity);
    if (!page)
        return module::Result::invalid_argument;
    module::RelationPage result;
    result.revision = w.relation_revision();
    result.count = static_cast<std::uint32_t>(page->keys.size());
    result.has_more = page->has_more ? 1U : 0U;
    result.cursor_valid = page->after ? 1U : 0U;
    if (page->after)
        result.after = relation_key(*page->after);
    for (std::uint32_t i = 0; i < result.count; ++i)
        result.entries[i] = relation_key(page->keys[i]);
    *output = result;
    return module::Result::success;
}
module::Result host_add_relation(void *value, module::RelationKey key) noexcept {
    const auto *b = current_bridge(value);
    if (!b || !b->context || !b->writable)
        return module::Result::wrong_phase;
    const auto k = relation_key(key);
    if (!k.valid())
        return module::Result::invalid_argument;
    auto &w = b->context->world;
    if (!w.find(k.subject) || !w.find(k.object))
        return module::Result::not_found;
    return w.has_relation(k) || w.add_relation(k) ? module::Result::success
                                                  : module::Result::invalid_argument;
}
module::Result host_remove_relation(void *value, module::RelationKey key) noexcept {
    const auto *b = current_bridge(value);
    if (!b || !b->context || !b->writable)
        return module::Result::wrong_phase;
    const auto k = relation_key(key);
    if (!k.valid())
        return module::Result::invalid_argument;
    return b->context->world.remove_relation(k) ? module::Result::success
                                                : module::Result::no_relation;
}
module::Result host_read_relation_component(void *value, module::RelationKey key,
                                            module::EntityId type,
                                            module::ComponentData *output) noexcept {
    const auto *b = current_bridge(value);
    if (!b || !b->context)
        return module::Result::wrong_phase;
    const auto k = relation_key(key);
    if (!output || output->size < sizeof(*output) || !k.valid() || persistent_id(type).is_nil())
        return module::Result::invalid_argument;
    const auto &w = b->context->world;
    if (!w.has_relation(k))
        return module::Result::no_relation;
    const auto data = w.relation_component(k, {persistent_id(type)});
    if (!data)
        return module::Result::no_component;
    *output = encode_component_data(*data);
    return module::Result::success;
}
module::Result host_set_relation_component(void *value, module::RelationKey key,
                                           const module::ComponentData *input) noexcept {
    const auto *b = current_bridge(value);
    if (!b || !b->context || !b->writable)
        return module::Result::wrong_phase;
    const auto k = relation_key(key);
    if (!k.valid())
        return module::Result::invalid_argument;
    const auto data = decode_component_data(input);
    if (!data)
        return module::Result::invalid_argument;
    auto &w = b->context->world;
    if (!w.has_relation(k))
        return module::Result::no_relation;
    const auto schema = w.component_type(data->type);
    if (!schema)
        return module::Result::not_found;
    if (schema->version != data->version)
        return module::Result::schema_conflict;
    return w.set_relation_component(k, *data) ? module::Result::success
                                              : module::Result::invalid_argument;
}
module::Result host_remove_relation_component(void *value, module::RelationKey key,
                                              module::EntityId type) noexcept {
    const auto *b = current_bridge(value);
    if (!b || !b->context || !b->writable)
        return module::Result::wrong_phase;
    const auto k = relation_key(key);
    if (!k.valid() || persistent_id(type).is_nil())
        return module::Result::invalid_argument;
    auto &w = b->context->world;
    if (!w.has_relation(k))
        return module::Result::no_relation;
    return w.remove_relation_component(k, {persistent_id(type)}) ? module::Result::success
                                                                 : module::Result::no_component;
}
module::Result host_read_world_clock(void* value,module::WorldClockSnapshot* output) noexcept {const auto* b=current_bridge(value);if(!b||!b->context)return module::Result::wrong_phase;if(!output||output->size<sizeof(*output))return module::Result::invalid_argument;const auto c=b->context->world.world_clock();module::WorldClockSnapshot result;result.ticks=c.ticks;result.ticks_per_second=world::world_ticks_per_second;result.rate_milli=c.rate_milli;result.rate_remainder=c.rate_remainder;result.input_fraction=c.input_fraction;result.paused=c.paused?1U:0U;result.automatic=c.automatic?1U:0U;*output=result;return module::Result::success;}
module::Result host_configure_world_clock(void* value,std::uint32_t paused,std::uint32_t rate,std::uint32_t automatic) noexcept {const auto* b=current_bridge(value);if(!b||!b->context||!b->writable)return module::Result::wrong_phase;if(paused>1||automatic>1)return module::Result::invalid_argument;return b->context->world.configure_world_clock(paused!=0,rate,automatic!=0)?module::Result::success:module::Result::invalid_argument;}
module::Result host_step_world_clock(void* value,std::uint64_t ticks) noexcept {const auto* b=current_bridge(value);if(!b||!b->context||!b->writable)return module::Result::wrong_phase;return b->context->world.step_world_clock(ticks)?module::Result::success:module::Result::invalid_argument;}
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

module::Result host_read_physical_rig(void *value, module::EntityId id,
                                      module::PhysicalRigSnapshot *out) noexcept {
    const auto *b = current_bridge(value);
    if (!b || !b->context)
        return module::Result::wrong_phase;
    if (!out || out->size < sizeof(*out) || persistent_id(id).is_nil())
        return module::Result::invalid_argument;
    const auto entity = b->context->world.find(persistent_id(id));
    if (!entity)
        return module::Result::not_found;
    const auto binding = b->context->world.physical_rig_binding(*entity);
    if (!binding)
        return module::Result::no_component;
    module::PhysicalRigSnapshot result;
    std::copy(binding->root.bytes().begin(), binding->root.bytes().end(), result.root.bytes);
    result.node = binding->node;
    std::copy(binding->translation.begin(), binding->translation.end(), result.translation);
    std::copy(binding->rotation.begin(), binding->rotation.end(), result.rotation);
    *out = result;
    return module::Result::success;
}
module::Result host_read_swing_twist_joint(void *value, module::EntityId id,
                                           module::SwingTwistSnapshot *out) noexcept {
    const auto *b = current_bridge(value);
    if (!b || !b->context)
        return module::Result::wrong_phase;
    if (!out || out->size < sizeof(*out) || persistent_id(id).is_nil())
        return module::Result::invalid_argument;
    const auto e = b->context->world.find(persistent_id(id));
    if (!e)
        return module::Result::not_found;
    const auto h = b->context->world.swing_twist_joint(*e);
    if (!h)
        return module::Result::no_link;
    module::SwingTwistSnapshot result;
    std::copy(h->first.bytes().begin(), h->first.bytes().end(), result.first.bytes);
    std::copy(h->second.bytes().begin(), h->second.bytes().end(), result.second.bytes);
    const auto &d = h->configuration.drive;
    result.drive.swing_mode = static_cast<module::JointDriveMode>(d.swing_mode);
    result.drive.twist_mode = static_cast<module::JointDriveMode>(d.twist_mode);
    std::copy(d.target_orientation.begin(), d.target_orientation.end(),
              result.drive.target_orientation);
    std::copy(d.target_velocity.begin(), d.target_velocity.end(), result.drive.target_velocity);
    result.drive.frequency_hz = d.frequency_hz;
    result.drive.damping_ratio = d.damping_ratio;
    result.drive.max_swing_axis_torque = d.max_swing_axis_torque;
    result.drive.max_twist_torque = d.max_twist_torque;
    result.enabled = h->enabled;
    result.swing_half_angle = h->configuration.swing_half_angle;
    result.twist_min = h->configuration.twist_min;
    result.twist_max = h->configuration.twist_max;
#if defined(VICE_RUNTIME_AUTHORED_PHYSICS)
    if (b->context->physics) {
        const auto o = b->context->physics->swing_twist_joint(persistent_id(id));
        if (!o)
            return module::Result::reset_required;
        result.simulated = 1;
        result.active = o->active;
        result.orientation[0] = o->orientation.x;
        result.orientation[1] = o->orientation.y;
        result.orientation[2] = o->orientation.z;
        result.orientation[3] = o->orientation.w;
        result.angular_error = o->angular_error;
        result.linear_impulse = o->linear_impulse;
        result.motor_impulses[0] = o->motor_impulses.x;
        result.motor_impulses[1] = o->motor_impulses.y;
        result.motor_impulses[2] = o->motor_impulses.z;
        result.limit_impulses[0] = o->limit_impulses.x;
        result.limit_impulses[1] = o->limit_impulses.y;
        result.limit_impulses[2] = o->limit_impulses.z;
    }
#endif
    *out = result;
    return module::Result::success;
}
module::Result host_control_swing_twist_joint(void *value, module::EntityId id,
                                              const module::SwingTwistDrive *d,
                                              std::uint32_t enabled) noexcept {
    const auto *b = current_bridge(value);
    if (!b || !b->context || !b->writable)
        return module::Result::wrong_phase;
    if (!d || d->size < sizeof(*d) || persistent_id(id).is_nil() || enabled > 1)
        return module::Result::invalid_argument;
    world::SwingTwistDrive drive;
    drive.swing_mode = static_cast<world::JointDriveMode>(d->swing_mode);
    drive.twist_mode = static_cast<world::JointDriveMode>(d->twist_mode);
    std::copy_n(d->target_orientation, 4, drive.target_orientation.begin());
    std::copy_n(d->target_velocity, 3, drive.target_velocity.begin());
    drive.frequency_hz = d->frequency_hz;
    drive.damping_ratio = d->damping_ratio;
    drive.max_swing_axis_torque = d->max_swing_axis_torque;
    drive.max_twist_torque = d->max_twist_torque;
    if (!world::valid(drive))
        return module::Result::invalid_argument;
    const auto e = b->context->world.find(persistent_id(id));
    if (!e)
        return module::Result::not_found;
    if (!b->context->world.swing_twist_joint(*e))
        return module::Result::no_link;
#if defined(VICE_RUNTIME_AUTHORED_PHYSICS)
    if (b->context->physics) {
        using R = rigid_body::BodyControlResult;
        switch (b->context->physics->control_swing_twist_joint(persistent_id(id), drive,
                                                               enabled != 0)) {
        case R::applied:
            return module::Result::success;
        case R::invalid_argument:
            return module::Result::invalid_argument;
        case R::inactive_region:
            return module::Result::inactive_region;
        default:
            return module::Result::reset_required;
        }
    }
#endif
    return module::Result::unavailable;
}
module::Result host_read_powered_hinge(void *value, module::EntityId id,
                                       module::PoweredHingeSnapshot *out) noexcept {
    const auto *b = current_bridge(value);
    if (!b || !b->context)
        return module::Result::wrong_phase;
    if (!out || out->size < sizeof(*out) || persistent_id(id).is_nil())
        return module::Result::invalid_argument;
    const auto e = b->context->world.find(persistent_id(id));
    if (!e)
        return module::Result::not_found;
    const auto h = b->context->world.powered_hinge(*e);
    if (!h)
        return module::Result::no_link;
    module::PoweredHingeSnapshot result;
    std::copy(h->first.bytes().begin(), h->first.bytes().end(), result.first.bytes);
    std::copy(h->second.bytes().begin(), h->second.bytes().end(), result.second.bytes);
    result.drive = {sizeof(module::JointDrive), static_cast<module::JointDriveMode>(h->drive.mode),
                    h->drive.target_angle,      h->drive.target_velocity,
                    h->drive.frequency_hz,      h->drive.damping_ratio,
                    h->drive.max_torque};
    result.enabled = h->enabled;
    result.min_angle = h->min_angle;
    result.max_angle = h->max_angle;
#if defined(VICE_RUNTIME_AUTHORED_PHYSICS)
    if (b->context->physics) {
        const auto o = b->context->physics->powered_hinge(persistent_id(id));
        if (!o)
            return module::Result::reset_required;
        result.simulated = 1;
        result.active = o->active;
        result.angle = o->angle;
        result.error = o->error;
        result.motor_impulse = o->motor_impulse;
        result.limit_impulse = o->limit_impulse;
        result.linear_impulse = o->linear_impulse;
    }
#endif
    *out = result;
    return module::Result::success;
}
module::Result host_control_powered_hinge(void *value, module::EntityId id,
                                          const module::JointDrive *d,
                                          std::uint32_t enabled) noexcept {
    const auto *b = current_bridge(value);
    if (!b || !b->context || !b->writable)
        return module::Result::wrong_phase;
    if (!d || d->size < sizeof(*d) || persistent_id(id).is_nil() || enabled > 1)
        return module::Result::invalid_argument;
    const world::JointDrive drive{static_cast<world::JointDriveMode>(d->mode),
                                  d->target_angle,
                                  d->target_velocity,
                                  d->frequency_hz,
                                  d->damping_ratio,
                                  d->max_torque};
    if (!world::valid(drive))
        return module::Result::invalid_argument;
    const auto e = b->context->world.find(persistent_id(id));
    if (!e)
        return module::Result::not_found;
    if (!b->context->world.powered_hinge(*e))
        return module::Result::no_link;
#if defined(VICE_RUNTIME_AUTHORED_PHYSICS)
    if (b->context->physics) {
        using R = rigid_body::BodyControlResult;
        switch (
            b->context->physics->control_powered_hinge(persistent_id(id), drive, enabled != 0)) {
        case R::applied:
            return module::Result::success;
        case R::invalid_argument:
            return module::Result::invalid_argument;
        case R::inactive_region:
            return module::Result::inactive_region;
        default:
            return module::Result::reset_required;
        }
    }
#endif
    return module::Result::unavailable;
}

module::Result host_read_animation(void* value,module::EntityId id,module::AnimationSnapshot* output) noexcept {
    const auto* b=current_bridge(value);if(!b||!b->context)return module::Result::wrong_phase;
    if(!output||output->size<sizeof(module::AnimationSnapshot)||persistent_id(id).is_nil())return module::Result::invalid_argument;
    const auto e=b->context->world.find(persistent_id(id));if(!e)return module::Result::not_found;const auto p=b->context->world.animation(*e);if(!p)return module::Result::no_animation;
    if(!b->context->presentation)return module::Result::unavailable;const auto o=b->context->presentation->animation(persistent_id(id));if(!o)return module::Result::reset_required;
    module::AnimationSnapshot result;result.clip=p->clip;result.playing=p->playing?1U:0U;result.looping=p->looping?1U:0U;result.rate=p->rate;result.time=p->time;result.duration=o->duration;result.node_count=static_cast<std::uint32_t>(o->node_count);result.part_count=static_cast<std::uint32_t>(o->part_count);std::copy_n(o->clip_name.data(),std::min(o->clip_name.size(),std::size_t{128}),result.clip_name);*output=result;return module::Result::success;
}
module::Result host_control_animation(void* value,module::EntityId id,std::uint32_t clip,float rate,std::uint32_t playing,std::uint32_t looping,std::uint32_t restart) noexcept {
    const auto* b=current_bridge(value);if(!b||!b->context||!b->writable)return module::Result::wrong_phase;
    if(persistent_id(id).is_nil()||clip>=64||!std::isfinite(rate)||rate<0||rate>8||playing>1||looping>1||restart>1)return module::Result::invalid_argument;
    const auto e=b->context->world.find(persistent_id(id));if(!e)return module::Result::not_found;if(!b->context->world.animation(*e))return module::Result::no_animation;if(!b->context->presentation)return module::Result::unavailable;
    if(!b->context->presentation->animation(persistent_id(id)))return module::Result::reset_required;
    return b->context->presentation->control_animation(persistent_id(id),clip,rate,playing!=0,looping!=0,restart!=0)?module::Result::success:module::Result::invalid_argument;
}
module::Result host_read_rig_node(void *value,module::EntityId id,std::uint32_t node,std::uint32_t physical,module::RigNodeSnapshot *out) noexcept {
    const auto *b=current_bridge(value);
    if(!b||!b->context)return module::Result::wrong_phase;
    if(!out||out->size<sizeof(module::RigNodeSnapshot)||persistent_id(id).is_nil()||node>=128||physical>1)return module::Result::invalid_argument;
    const auto entity=b->context->world.find(persistent_id(id));if(!entity)return module::Result::not_found;
    if(!b->context->world.animation(*entity))return module::Result::no_animation;
    if(!b->context->presentation)return module::Result::unavailable;
    const auto pose=b->context->presentation->node_pose(persistent_id(id),node,physical!=0);
    const auto spatial=b->context->world.spatial_state(*entity);
    if(!pose||!spatial)return module::Result::invalid_argument;
    const auto t=b->context->world.authored_transform(*entity).value_or(world::AuthoredTransform{});
    if(t.scale.x!=t.scale.y||t.scale.y!=t.scale.z)return module::Result::invalid_argument;
    const auto global=presentation::compose({{}, {t.rotation.x,t.rotation.y,t.rotation.z,t.rotation.w},t.scale.x},*pose);
    module::RigNodeSnapshot result;result.node=node;result.physical=physical;
    result.position[0]=spatial->global_position.x+global.translation.x;
    result.position[1]=spatial->global_position.y+global.translation.y;
    result.position[2]=spatial->global_position.z+global.translation.z;
    result.rotation[0]=global.rotation.x;result.rotation[1]=global.rotation.y;result.rotation[2]=global.rotation.z;result.rotation[3]=global.rotation.w;result.scale=global.scale;
    *out=result;return module::Result::success;
}
module::Result host_control_animation_mix(void *value,module::EntityId id,std::uint32_t base,std::uint32_t other,float weight,float rate) noexcept {
    const auto *b=current_bridge(value);
    if(!b||!b->context||!b->writable)return module::Result::wrong_phase;
    if(persistent_id(id).is_nil()||base>=64||other>=64||!std::isfinite(weight)||weight<0||weight>1||!std::isfinite(rate)||rate<0||rate>8)return module::Result::invalid_argument;
    const auto e=b->context->world.find(persistent_id(id));if(!e)return module::Result::not_found;
    if(!b->context->world.animation(*e))return module::Result::no_animation;
    if(!b->context->presentation)return module::Result::unavailable;
    if(!b->context->presentation->animation(persistent_id(id)))return module::Result::reset_required;
    return b->context->presentation->control_animation_mix(persistent_id(id),base,other,weight,rate)?module::Result::success:module::Result::invalid_argument;
}
module::Result host_read_audio(void* value,module::EntityId id,module::AudioSnapshot* output) noexcept {
    const auto* b=current_bridge(value);if(!b||!b->context)return module::Result::wrong_phase;if(!output||output->size<sizeof(module::AudioSnapshot)||persistent_id(id).is_nil())return module::Result::invalid_argument;
    const auto e=b->context->world.find(persistent_id(id));if(!e)return module::Result::not_found;const auto p=b->context->world.audio_emitter(*e);if(!p)return module::Result::no_audio;if(!b->context->presentation)return module::Result::unavailable;const auto o=b->context->presentation->audio(persistent_id(id));if(!o)return module::Result::reset_required;
    module::AudioSnapshot result;result.playing=p->playing?1U:0U;result.looping=p->looping?1U:0U;result.output_active=o->output_active?1U:0U;result.gain=p->gain;result.pitch=p->pitch;result.reference_distance=p->reference_distance;result.max_distance=p->max_distance;result.time=p->time;result.duration=o->duration;result.left_gain=o->left_gain;result.right_gain=o->right_gain;result.distance=o->distance;result.delivered_frames=o->delivered_frames;result.underrun_frames=o->underrun_frames;*output=result;return module::Result::success;
}
module::Result host_control_audio(void* value,module::EntityId id,float gain,float pitch,std::uint32_t playing,std::uint32_t restart) noexcept {
    const auto* b=current_bridge(value);if(!b||!b->context||!b->writable)return module::Result::wrong_phase;if(persistent_id(id).is_nil()||!std::isfinite(gain)||gain<0||gain>1||!std::isfinite(pitch)||pitch<0.25F||pitch>4||playing>1||restart>1)return module::Result::invalid_argument;
    const auto e=b->context->world.find(persistent_id(id));if(!e)return module::Result::not_found;if(!b->context->world.audio_emitter(*e))return module::Result::no_audio;if(!b->context->presentation)return module::Result::unavailable;
    if(!b->context->presentation->audio(persistent_id(id)))return module::Result::reset_required;
    return b->context->presentation->control_audio(persistent_id(id),gain,pitch,playing!=0,restart!=0)?module::Result::success:module::Result::invalid_argument;
}
module::Result host_set_character_facing(void* value,module::EntityId id,module::Quaternion q) noexcept {
    const auto* b=current_bridge(value);if(!b||!b->context||!b->writable)return module::Result::wrong_phase;if(persistent_id(id).is_nil()||!world::valid(world::AuthoredRotation{q.x,q.y,q.z,q.w}))return module::Result::invalid_argument;
    const auto e=b->context->world.find(persistent_id(id));if(!e)return module::Result::not_found;if(!b->context->world.character(*e))return module::Result::no_character;auto t=b->context->world.authored_transform(*e).value_or(world::AuthoredTransform{});t.rotation={q.x,q.y,q.z,q.w};return b->context->world.set_authored_transform(*e,t)?module::Result::success:module::Result::invalid_argument;
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
module::EventRef encode_event_ref(const world::EventRef &ref) {
    return {module_id(ref.history), module_id(ref.id), ref.sequence};
}
module::WorldEventSnapshot encode_world_event(const world::World &w, const world::CausalEvent &e,
                                              world::PersistentId stream, bool retained) {
    module::WorldEventSnapshot out;
    out.kind = static_cast<std::uint32_t>(e.kind);
    out.origin = static_cast<std::uint32_t>(e.origin);
    out.retained = retained ? 1U : 0U;
    out.ref = encode_event_ref({stream, e.id, e.sequence});
    out.world_ticks = e.world_ticks;
    out.region = e.region.value;
    out.subject = module_id(e.subject);
    out.target = module_id(e.first);
    out.object = module_id(e.second);
    if (e.data) {
        out.has_payload = 1;
        out.payload = encode_component_data(e.data->payload);
        out.parent_count = static_cast<std::uint32_t>(e.data->parents.size());
        for (std::size_t i = 0; i < e.data->parents.size(); ++i)
            out.parents[i] = encode_event_ref(e.data->parents[i]);
    } else if (e.parent_sequence) {
        out.parent_count = 1;
        out.parents[0] = encode_event_ref({stream, e.parent_id, e.parent_sequence});
    }
    for (std::uint32_t i = 0; i < out.parent_count; ++i) {
        const auto parent = w.causal_event(out.parents[i].sequence);
        out.parent_retained[i] = parent && parent->id == persistent_id(out.parents[i].id) ? 1U : 0U;
    }
    return out;
}
module::Result host_emit_world_event(void *value, const module::WorldEventRequest *input,
                                     module::EventRef *output) noexcept {
    const auto *b = current_bridge(value);
    if (!b || !b->context || !b->writable)
        return module::Result::wrong_phase;
    if (!input || input->size < sizeof(*input) || !output || input->retained > 1 ||
        input->parent_count > world::world_event_parent_capacity)
        return module::Result::invalid_argument;
    const auto payload = decode_component_data(&input->payload);
    if (!payload)
        return module::Result::invalid_argument;
    world::WorldEventRequest request;
    request.subject = persistent_id(input->subject);
    request.target = persistent_id(input->target);
    request.object = persistent_id(input->object);
    request.region = {input->region};
    request.retained = input->retained != 0;
    request.data.payload = *payload;
    for (std::uint32_t i = 0; i < input->parent_count; ++i) {
        const auto &p = input->parents[i];
        request.data.parents.push_back({persistent_id(p.history), persistent_id(p.id), p.sequence});
    }
    const auto result = b->context->world.emit_world_event(std::move(request));
    using R = world::WorldEventResult;
    switch (result.result) {
    case R::success:
        *output = encode_event_ref(result.ref);
        return module::Result::success;
    case R::missing_entity:
    case R::missing_schema:
        return module::Result::not_found;
    case R::schema_conflict:
        return module::Result::schema_conflict;
    case R::history_mismatch:
        return module::Result::history_mismatch;
    case R::parent_pruned:
        return module::Result::event_pruned;
    case R::invalid_parent:
        return module::Result::invalid_parent;
    case R::sequence_exhausted:
        return module::Result::sequence_exhausted;
    case R::invalid_argument:
        return module::Result::invalid_argument;
    }
    return module::Result::invalid_argument;
}
module::Result host_read_world_event(void *value, module::EntityId history, std::uint64_t sequence,
                                     module::WorldEventSnapshot *output) noexcept {
    const auto *b = current_bridge(value);
    if (!b || !b->context)
        return module::Result::wrong_phase;
    if (!output || output->size < sizeof(*output) || !sequence || persistent_id(history).is_nil())
        return module::Result::invalid_argument;
    const auto &w = b->context->world;
    const auto info = w.causal_info();
    if (info.id != persistent_id(history))
        return module::Result::history_mismatch;
    if (sequence < info.first_sequence)
        return module::Result::event_pruned;
    const auto e = w.causal_event(sequence);
    if (!e)
        return module::Result::not_found;
    *output = encode_world_event(w, *e, info.id, true);
    return module::Result::success;
}
module::Result host_read_signal_window(void *value, module::SignalWindowSnapshot *output) noexcept {
    const auto *b = current_bridge(value);
    if (!b || !b->context)
        return module::Result::wrong_phase;
    if (!output || output->size < sizeof(*output))
        return module::Result::invalid_argument;
    const auto info = b->context->world.signal_info();
    module::SignalWindowSnapshot out;
    out.stream = module_id(info.stream);
    out.first_sequence = info.first_sequence;
    out.next_sequence = info.next_sequence;
    out.retained_count = static_cast<std::uint32_t>(info.retained_count);
    *output = out;
    return module::Result::success;
}
module::Result host_read_world_signal(void *value, module::EntityId stream, std::uint64_t sequence,
                                      module::WorldEventSnapshot *output) noexcept {
    const auto *b = current_bridge(value);
    if (!b || !b->context)
        return module::Result::wrong_phase;
    if (!output || output->size < sizeof(*output) || !sequence || persistent_id(stream).is_nil())
        return module::Result::invalid_argument;
    const auto &w = b->context->world;
    const auto info = w.signal_info();
    if (info.stream != persistent_id(stream))
        return module::Result::history_mismatch;
    if (sequence < info.first_sequence)
        return module::Result::event_pruned;
    const auto e = w.world_signal(sequence);
    if (!e)
        return module::Result::not_found;
    *output = encode_world_event(w, *e, info.stream, false);
    return module::Result::success;
}
std::optional<world::WorldEventRequest>
decode_event_request(const module::WorldEventRequest *input) {
    if (!input || input->size < sizeof(*input) || input->retained > 1 ||
        input->parent_count > world::world_event_parent_capacity)
        return {};
    const auto payload = decode_component_data(&input->payload);
    if (!payload)
        return {};
    world::WorldEventRequest out;
    out.subject = persistent_id(input->subject);
    out.target = persistent_id(input->target);
    out.object = persistent_id(input->object);
    out.region = {input->region};
    out.retained = input->retained != 0;
    out.data.payload = *payload;
    for (std::uint32_t i = 0; i < input->parent_count; ++i) {
        const auto &p = input->parents[i];
        out.data.parents.push_back({persistent_id(p.history), persistent_id(p.id), p.sequence});
    }
    return out;
}
module::WorldEventRequest encode_event_request(const world::WorldEventRequest &e) {
    module::WorldEventRequest out;
    out.subject = module_id(e.subject);
    out.target = module_id(e.target);
    out.object = module_id(e.object);
    out.region = e.region.value;
    out.retained = e.retained ? 1U : 0U;
    out.payload = encode_component_data(e.data.payload);
    out.parent_count = static_cast<std::uint32_t>(e.data.parents.size());
    for (std::size_t i = 0; i < e.data.parents.size(); ++i)
        out.parents[i] = encode_event_ref(e.data.parents[i]);
    return out;
}
module::Result event_result(world::WorldEventResult r) {
    using R = world::WorldEventResult;
    switch (r) {
    case R::success:
        return module::Result::success;
    case R::invalid_argument:
        return module::Result::invalid_argument;
    case R::missing_entity:
    case R::missing_schema:
        return module::Result::not_found;
    case R::schema_conflict:
        return module::Result::schema_conflict;
    case R::history_mismatch:
        return module::Result::history_mismatch;
    case R::parent_pruned:
        return module::Result::event_pruned;
    case R::invalid_parent:
        return module::Result::invalid_parent;
    case R::sequence_exhausted:
        return module::Result::sequence_exhausted;
    }
    return module::Result::invalid_argument;
}
module::ScheduleSummary encode_schedule_summary(const world::WorldSchedule &s) {
    module::ScheduleSummary out;
    out.id = module_id(s.id);
    out.subject = module_id(s.event.subject);
    out.target = module_id(s.event.target);
    out.object = module_id(s.event.object);
    out.type = module_id(s.event.data.payload.type.value);
    out.region = s.event.region.value;
    out.due_ticks = s.due_ticks;
    out.interval_ticks = s.interval_ticks;
    out.occurrence = s.occurrence;
    out.catch_up = static_cast<module::ScheduleCatchUp>(s.catch_up);
    out.failure = static_cast<module::ScheduleFailure>(s.failure);
    out.event_result = event_result(s.event_result);
    out.retained = s.event.retained ? 1U : 0U;
    return out;
}
module::Result host_schedule_event(void *value, const module::ScheduleRequest *input,
                                   module::EntityId *output) noexcept {
    const auto *b = current_bridge(value);
    if (!b || !b->context || !b->writable)
        return module::Result::wrong_phase;
    if (!input || input->size < sizeof(*input) || !output ||
        static_cast<unsigned>(input->catch_up) > 1)
        return module::Result::invalid_argument;
    const auto e = decode_event_request(&input->event);
    if (!e)
        return module::Result::invalid_argument;
    const auto result = b->context->world.schedule_event(
        *e, input->due_ticks, input->interval_ticks,
        static_cast<world::ScheduleCatchUp>(input->catch_up), persistent_id(input->id));
    using R = world::ScheduleResult;
    switch (result.result) {
    case R::success:
        *output = module_id(result.id);
        return module::Result::success;
    case R::event_rejected:
        return event_result(result.event_result);
    case R::duplicate_id:
        return module::Result::duplicate_id;
    case R::capacity_exceeded:
        return module::Result::capacity_exceeded;
    case R::not_found:
        return module::Result::no_schedule;
    case R::invalid_argument:
        return module::Result::invalid_argument;
    }
    return module::Result::invalid_argument;
}
module::Result host_cancel_schedule(void *value, module::EntityId id) noexcept {
    const auto *b = current_bridge(value);
    if (!b || !b->context || !b->writable)
        return module::Result::wrong_phase;
    if (persistent_id(id).is_nil())
        return module::Result::invalid_argument;
    return b->context->world.cancel_schedule(persistent_id(id)) ? module::Result::success
                                                                : module::Result::no_schedule;
}
module::Result host_read_schedule(void *value, module::EntityId id,
                                  module::ScheduleSnapshot *output) noexcept {
    const auto *b = current_bridge(value);
    if (!b || !b->context)
        return module::Result::wrong_phase;
    if (!output || output->size < sizeof(*output) || persistent_id(id).is_nil())
        return module::Result::invalid_argument;
    const auto s = b->context->world.schedule(persistent_id(id));
    if (!s)
        return module::Result::no_schedule;
    module::ScheduleSnapshot out;
    out.summary = encode_schedule_summary(*s);
    out.event = encode_event_request(s->event);
    *output = out;
    return module::Result::success;
}
module::Result host_read_schedules(void *value, module::SchedulePageSnapshot *output) noexcept {
    const auto *b = current_bridge(value);
    if (!b || !b->context)
        return module::Result::wrong_phase;
    if (!output || output->size < sizeof(*output))
        return module::Result::invalid_argument;
    const auto page = b->context->world.schedule_page(persistent_id(output->after));
    module::SchedulePageSnapshot out;
    out.after = output->after;
    out.count = static_cast<std::uint32_t>(page.entries.size());
    out.has_more = page.has_more ? 1U : 0U;
    for (std::size_t i = 0; i < page.entries.size(); ++i)
        out.entries[i] = encode_schedule_summary(page.entries[i]);
    if (out.count)
        out.after = out.entries[out.count - 1].id;
    *output = out;
    return module::Result::success;
}
module::Result host_read_scheduler(void *value, module::SchedulerSnapshot *output) noexcept {
    const auto *b = current_bridge(value);
    if (!b || !b->context)
        return module::Result::wrong_phase;
    if (!output || output->size < sizeof(*output))
        return module::Result::invalid_argument;
    const auto info = b->context->world.scheduler_info();
    module::SchedulerSnapshot out;
    out.count = static_cast<std::uint32_t>(info.count);
    out.pending = static_cast<std::uint32_t>(info.pending);
    out.failed = static_cast<std::uint32_t>(info.failed);
    out.has_next = info.next_due ? 1U : 0U;
    out.next_due_ticks = info.next_due.value_or(0);
    out.due_remaining = info.due_remaining ? 1U : 0U;
    *output = out;
    return module::Result::success;
}
module::Result host_dispatch_schedules(void *value, std::uint32_t budget,
                                       module::ScheduleDispatchSnapshot *output) noexcept {
    const auto *b = current_bridge(value);
    if (!b || !b->context || !b->writable)
        return module::Result::wrong_phase;
    if (!output || output->size < sizeof(*output) || !budget ||
        budget > world::world_schedule_dispatch_budget)
        return module::Result::invalid_argument;
    const auto result = b->context->world.dispatch_schedules(budget);
    module::ScheduleDispatchSnapshot out;
    out.processed = static_cast<std::uint32_t>(result.processed);
    out.fired = static_cast<std::uint32_t>(result.fired);
    out.failed = static_cast<std::uint32_t>(result.failed);
    out.due_remaining = result.due_remaining ? 1U : 0U;
    *output = out;
    return module::Result::success;
}
module::Result host_read_event_schedule(void *value, module::EventRef ref, std::uint32_t retained,
                                        module::EventScheduleSnapshot *output) noexcept {
    const auto *b = current_bridge(value);
    if (!b || !b->context)
        return module::Result::wrong_phase;
    if (!output || output->size < sizeof(*output) || retained > 1 || persistent_id(ref.id).is_nil())
        return module::Result::invalid_argument;
    module::WorldEventSnapshot check;
    const auto result = retained ? host_read_world_event(value, ref.history, ref.sequence, &check)
                                 : host_read_world_signal(value, ref.history, ref.sequence, &check);
    if (result != module::Result::success)
        return result;
    if (persistent_id(check.ref.id) != persistent_id(ref.id))
        return module::Result::invalid_parent;
    const auto e = retained ? b->context->world.causal_event(ref.sequence)
                            : b->context->world.world_signal(ref.sequence);
    module::EventScheduleSnapshot out;
    if (e->data && e->data->schedule) {
        const auto &s = *e->data->schedule;
        out.has_schedule = 1;
        out.id = module_id(s.id);
        out.due_ticks = s.due_ticks;
        out.occurrence = s.occurrence;
        out.count = s.count;
    }
    *output = out;
    return module::Result::success;
}
world::QuantityKey quantity_key(module::QuantityKey key) {
    return {persistent_id(key.holder), {persistent_id(key.type)}};
}
module::Result quantity_result(world::QuantityResult r) {
    using R = world::QuantityResult;
    switch (r) {
    case R::success:
        return module::Result::success;
    case R::invalid_argument:
        return module::Result::invalid_argument;
    case R::invalid_type:
        return module::Result::invalid_quantity_type;
    case R::invalid_owner:
        return module::Result::invalid_quantity_owner;
    case R::missing_account:
        return module::Result::no_quantity_account;
    case R::insufficient_quantity:
        return module::Result::insufficient_quantity;
    case R::capacity_exceeded:
        return module::Result::quantity_capacity_exceeded;
    case R::nonempty_account:
        return module::Result::quantity_not_empty;
    case R::duplicate_account:
        return module::Result::quantity_account_exists;
    case R::schema_conflict:
        return module::Result::schema_conflict;
    case R::storage_limit:
        return module::Result::capacity_exceeded;
    case R::event_rejected:
        return module::Result::quantity_event_rejected;
    }
    return module::Result::invalid_argument;
}
module::QuantityTypeSnapshot encode_quantity_type(const world::QuantityTypeDesc &t) {
    module::QuantityTypeSnapshot out;
    out.id = module_id(t.id.value);
    out.version = t.version;
    out.quanta_per_unit = t.quanta_per_unit;
    std::copy(t.name.begin(), t.name.end(), out.name);
    return out;
}
module::QuantityAccount encode_quantity_account(const world::QuantityAccount &a) {
    return {{module_id(a.key.holder), module_id(a.key.type.value)}, a.amount, a.capacity};
}
module::Result host_register_quantity_type(void *value,
                                           const module::QuantityTypeSnapshot *input) noexcept {
    const auto *b = current_bridge(value);
    if (!b || !b->context || !b->writable)
        return module::Result::wrong_phase;
    if (!input || input->size < sizeof(*input))
        return module::Result::invalid_argument;
    const auto name = component_string(input->name);
    if (!name)
        return module::Result::invalid_argument;
    world::QuantityTypeDesc t{
        {persistent_id(input->id)}, *name, input->version, input->quanta_per_unit};
    if (!world::valid_quantity_type(t))
        return module::Result::invalid_argument;
    if (b->context->world.register_quantity_type(t))
        return module::Result::success;
    if (b->context->world.quantity_type_count() >= world::quantity_type_capacity &&
        !b->context->world.quantity_type(t.id))
        return module::Result::capacity_exceeded;
    return module::Result::schema_conflict;
}
module::Result host_remove_quantity_type(void *value, module::EntityId id) noexcept {
    const auto *b = current_bridge(value);
    if (!b || !b->context || !b->writable)
        return module::Result::wrong_phase;
    if (persistent_id(id).is_nil())
        return module::Result::invalid_argument;
    if (!b->context->world.quantity_type({persistent_id(id)}))
        return module::Result::invalid_quantity_type;
    return b->context->world.remove_quantity_type({persistent_id(id)})
               ? module::Result::success
               : module::Result::quantity_type_in_use;
}
module::Result host_read_quantity_type(void *value, module::EntityId id,
                                       module::QuantityTypeSnapshot *output) noexcept {
    const auto *b = current_bridge(value);
    if (!b || !b->context)
        return module::Result::wrong_phase;
    if (!output || output->size < sizeof(*output) || persistent_id(id).is_nil())
        return module::Result::invalid_argument;
    const auto t = b->context->world.quantity_type({persistent_id(id)});
    if (!t)
        return module::Result::invalid_quantity_type;
    *output = encode_quantity_type(*t);
    return module::Result::success;
}
module::Result host_read_quantity_types(void *value,
                                        module::QuantityTypePageSnapshot *output) noexcept {
    const auto *b = current_bridge(value);
    if (!b || !b->context)
        return module::Result::wrong_phase;
    if (!output || output->size < sizeof(*output))
        return module::Result::invalid_argument;
    const auto page = b->context->world.quantity_type_page(persistent_id(output->after));
    module::QuantityTypePageSnapshot out;
    out.after = output->after;
    out.count = static_cast<std::uint32_t>(page.entries.size());
    out.has_more = page.has_more ? 1U : 0U;
    for (std::size_t i = 0; i < page.entries.size(); ++i)
        out.entries[i] = encode_quantity_type(page.entries[i]);
    if (out.count)
        out.after = out.entries[out.count - 1].id;
    *output = out;
    return module::Result::success;
}
module::Result host_create_quantity_account(void *value, module::QuantityKey key,
                                            std::int64_t capacity) noexcept {
    const auto *b = current_bridge(value);
    if (!b || !b->context || !b->writable)
        return module::Result::wrong_phase;
    return quantity_result(b->context->world.create_quantity_account(quantity_key(key), capacity));
}
module::Result host_set_quantity_capacity(void *value, module::QuantityKey key,
                                          std::int64_t capacity) noexcept {
    const auto *b = current_bridge(value);
    if (!b || !b->context || !b->writable)
        return module::Result::wrong_phase;
    return quantity_result(b->context->world.set_quantity_capacity(quantity_key(key), capacity));
}
module::Result host_remove_quantity_account(void *value, module::QuantityKey key) noexcept {
    const auto *b = current_bridge(value);
    if (!b || !b->context || !b->writable)
        return module::Result::wrong_phase;
    return quantity_result(b->context->world.remove_quantity_account(quantity_key(key)));
}
module::Result host_read_quantity_account(void *value, module::QuantityKey key,
                                          module::QuantityAccountSnapshot *output) noexcept {
    const auto *b = current_bridge(value);
    if (!b || !b->context)
        return module::Result::wrong_phase;
    if (!output || output->size < sizeof(*output))
        return module::Result::invalid_argument;
    const auto k = quantity_key(key);
    if (!b->context->world.quantity_type(k.type))
        return module::Result::invalid_quantity_type;
    if (!b->context->world.find(k.holder))
        return module::Result::invalid_quantity_owner;
    const auto a = b->context->world.quantity_account(k);
    if (!a)
        return module::Result::no_quantity_account;
    module::QuantityAccountSnapshot out;
    out.account = encode_quantity_account(*a);
    *output = out;
    return module::Result::success;
}
module::Result host_read_quantity_accounts(void *value, module::EntityId holder,
                                           module::QuantityAccountsSnapshot *output) noexcept {
    const auto *b = current_bridge(value);
    if (!b || !b->context)
        return module::Result::wrong_phase;
    if (!output || output->size < sizeof(*output))
        return module::Result::invalid_argument;
    if (!b->context->world.find(persistent_id(holder)))
        return module::Result::invalid_quantity_owner;
    const auto entries = b->context->world.quantity_accounts(persistent_id(holder));
    module::QuantityAccountsSnapshot out;
    out.count = static_cast<std::uint32_t>(entries.size());
    for (std::size_t i = 0; i < entries.size(); ++i)
        out.entries[i] = encode_quantity_account(entries[i]);
    *output = out;
    return module::Result::success;
}
module::Result host_transact_quantity(void *value, const module::QuantityTransactionRequest *input,
                                      module::QuantityReceiptSnapshot *output) noexcept {
    const auto *b = current_bridge(value);
    if (!b || !b->context || !b->writable)
        return module::Result::wrong_phase;
    if (!input || input->size < sizeof(*input) || !output || output->size < sizeof(*output) ||
        input->parent_count > world::world_event_parent_capacity ||
        static_cast<unsigned>(input->operation) > 2)
        return module::Result::invalid_argument;
    world::QuantityTransaction t;
    t.operation = static_cast<world::QuantityOperation>(input->operation);
    t.type = {persistent_id(input->type)};
    t.source = persistent_id(input->source);
    t.target = persistent_id(input->target);
    t.amount = input->amount;
    for (std::uint32_t i = 0; i < input->parent_count; ++i) {
        const auto &p = input->parents[i];
        t.parents.push_back({persistent_id(p.history), persistent_id(p.id), p.sequence});
    }
    const auto receipt = b->context->world.transact_quantity(t);
    module::QuantityReceiptSnapshot out;
    out.result = quantity_result(receipt.result);
    out.event_result = event_result(receipt.event_result);
    out.event = encode_event_ref(receipt.event);
    out.source_before = receipt.source_before;
    out.source_after = receipt.source_after;
    out.target_before = receipt.target_before;
    out.target_after = receipt.target_after;
    *output = out;
    return out.result;
}
#include "rule_module.inl"
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
        host_.emit_world_event = &host_emit_world_event;
        host_.read_world_event = &host_read_world_event;
        host_.read_signal_window = &host_read_signal_window;
        host_.read_world_signal = &host_read_world_signal;
        host_.register_quantity_type = &host_register_quantity_type;
        host_.remove_quantity_type = &host_remove_quantity_type;
        host_.read_quantity_type = &host_read_quantity_type;
        host_.read_quantity_types = &host_read_quantity_types;
        host_.create_quantity_account = &host_create_quantity_account;
        host_.set_quantity_capacity = &host_set_quantity_capacity;
        host_.remove_quantity_account = &host_remove_quantity_account;
        host_.read_quantity_account = &host_read_quantity_account;
        host_.read_quantity_accounts = &host_read_quantity_accounts;
        host_.transact_quantity = &host_transact_quantity;
        host_.add_rule = &host_add_rule;
        host_.remove_rule = &host_remove_rule;
        host_.set_rule_enabled = &host_set_rule_enabled;
        host_.read_rule = &host_read_rule;
        host_.read_rules = &host_read_rules;
        host_.read_rule_info = &host_read_rule_info;
        host_.dispatch_rules = &host_dispatch_rules;

        host_.schedule_event = &host_schedule_event;
        host_.cancel_schedule = &host_cancel_schedule;
        host_.read_schedule = &host_read_schedule;
        host_.read_schedules = &host_read_schedules;
        host_.read_scheduler = &host_read_scheduler;
        host_.dispatch_schedules = &host_dispatch_schedules;
        host_.read_event_schedule = &host_read_event_schedule;
        host_.read_world_clock=&host_read_world_clock;host_.configure_world_clock=&host_configure_world_clock;host_.step_world_clock=&host_step_world_clock;
        host_.read_relation=&host_read_relation;host_.read_relations=&host_read_relations;host_.add_relation=&host_add_relation;host_.remove_relation=&host_remove_relation;host_.read_relation_component=&host_read_relation_component;host_.set_relation_component=&host_set_relation_component;host_.remove_relation_component=&host_remove_relation_component;
        host_.register_component_schema=&host_register_component_schema;host_.read_component_schema=&host_read_component_schema;host_.read_component=&host_read_component;host_.set_component=&host_set_component;host_.remove_component=&host_remove_component;
        host_.read_physical_rig = &host_read_physical_rig;
        host_.read_swing_twist_joint = &host_read_swing_twist_joint;
        host_.control_swing_twist_joint = &host_control_swing_twist_joint;
        host_.read_powered_hinge = &host_read_powered_hinge;
        host_.control_powered_hinge = &host_control_powered_hinge;
        host_.read_rig_node=&host_read_rig_node;host_.control_animation_mix=&host_control_animation_mix;host_.read_animation=&host_read_animation;host_.control_animation=&host_control_animation;
        host_.read_audio=&host_read_audio;host_.control_audio=&host_control_audio;host_.set_character_facing=&host_set_character_facing;
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
