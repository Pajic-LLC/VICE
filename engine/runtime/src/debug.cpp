#include <vice/runtime/debug.hpp>
#include <vice/presentation/system.hpp>
#if defined(VICE_RUNTIME_AUTHORED_PHYSICS)
#include <vice/rigid_body/authored_simulation.hpp>
#endif

#include <vice/world/snapshot.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <limits>

namespace vice::runtime {
namespace {

[[nodiscard]] constexpr std::string_view debug_state_name(
    const RuntimeDebugEntityState state
) noexcept {
    switch (state) {
    case RuntimeDebugEntityState::available: return "available";
    case RuntimeDebugEntityState::not_found: return "not_found";
    case RuntimeDebugEntityState::invalid_request: return "invalid_request";
    }
    return "invalid_request";
}

[[nodiscard]] constexpr std::string_view fidelity_name(
    const world::RegionFidelity fidelity
) noexcept {
    switch (fidelity) {
    case world::RegionFidelity::full: return "full";
    case world::RegionFidelity::reduced: return "reduced";
    case world::RegionFidelity::inactive: return "inactive";
    }
    return "inactive";
}

void append_json_string(std::string& output, const std::string_view value) {
    output.push_back('"');
    for (const char character : value) {
        switch (character) {
        case '"': output += "\\\""; break;
        case '\\': output += "\\\\"; break;
        case '\n': output += "\\n"; break;
        case '\r': output += "\\r"; break;
        case '\t': output += "\\t"; break;
        default:
            if (static_cast<unsigned char>(character) < 0x20U) {
                output += "?";
            } else {
                output.push_back(character);
            }
            break;
        }
    }
    output.push_back('"');
}

void append_double(std::string& output, const double value) {
    if (!std::isfinite(value)) {
        output += "null";
        return;
    }
    std::array<char, 64> buffer{};
    const auto converted = std::to_chars(
        buffer.data(),
        buffer.data() + buffer.size(),
        value,
        std::chars_format::general,
        std::numeric_limits<double>::max_digits10
    );
    if (converted.ec != std::errc{}) {
        output += "null";
        return;
    }
    output.append(buffer.data(), converted.ptr);
}

void append_float(std::string& output, const float value) {
    append_double(output, static_cast<double>(value));
}

} // namespace

RuntimeDebugEntitySnapshot sample_runtime_debug_entity(
    const world::World& world,
    const world::PersistentId persistent_id,
    const rigid_body::AuthoredSimulation* physics,
    const presentation::System* presentation
) {
    RuntimeDebugEntitySnapshot result;
    result.requested_id = persistent_id;
    if (persistent_id.is_nil()) {
        result.state = RuntimeDebugEntityState::invalid_request;
        return result;
    }

    result.causal_history=world.causal_context(persistent_id);
    const auto snapshot = world.snapshot();
    const auto entity = std::find_if(
        snapshot.entities.begin(),
        snapshot.entities.end(),
        [&](const world::SnapshotEntity& candidate) {
            return candidate.persistent_id == persistent_id;
        }
    );
    if (entity == snapshot.entities.end()) {
        result.state = RuntimeDebugEntityState::not_found;
        return result;
    }

    result.state = RuntimeDebugEntityState::available;
    result.spatial = entity->spatial_state;
    result.authored_transform = entity->authored_transform;
    result.animation=entity->animation;result.rig_part=entity->rig_part;result.audio_emitter=entity->audio_emitter;
    if(presentation) {
        if(const auto a=presentation->animation(persistent_id)) {
            result.animation_state="{\"clip_name\":";append_json_string(result.animation_state,a->clip_name);result.animation_state+=",\"duration\":";append_float(result.animation_state,a->duration);result.animation_state+=",\"node_count\":"+std::to_string(a->node_count)+",\"part_count\":"+std::to_string(a->part_count)+'}';
        }
        if(const auto a=presentation->audio(persistent_id)) {
            auto& o=result.audio_state;o="{\"duration\":";append_float(o,a->duration);o+=",\"left_gain\":";append_float(o,a->left_gain);o+=",\"right_gain\":";append_float(o,a->right_gain);o+=",\"distance\":";append_float(o,a->distance);o+=",\"output_active\":";o+=a->output_active?"true":"false";o+=",\"delivered_frames\":"+std::to_string(a->delivered_frames)+",\"underrun_frames\":"+std::to_string(a->underrun_frames)+'}';
        }
    }
    result.character = entity->character;
    result.character_attachment=entity->character_attachment;
    result.vehicle = entity->vehicle;
#if defined(VICE_RUNTIME_AUTHORED_PHYSICS)
    if(physics && result.vehicle) if(const auto v=physics->vehicle(persistent_id)) {
        VehicleRuntimeDebug debug;debug.forward_speed=v->forward_speed;
        for(std::size_t i=0;i<4;++i) {
            const auto& w=v->wheels[i];
            debug.wheels[i]={w.grounded,w.supporting_entity,w.material,w.point,w.normal,w.suspension_length,w.normal_force,w.longitudinal_force,w.lateral_force};
        }
        result.vehicle_state=debug;
    }
    if (physics && result.character) {
        if (const auto c = physics->character(persistent_id)) {
            std::string ground;
            switch (c->state.ground_state) {
            case rigid_body::CharacterGroundState::grounded: ground="grounded"; break;
            case rigid_body::CharacterGroundState::steep_ground: ground="steep_ground"; break;
            case rigid_body::CharacterGroundState::unsupported: ground="unsupported"; break;
            case rigid_body::CharacterGroundState::airborne: ground="airborne"; break;
            }
            result.character_state = CharacterRuntimeDebug{ground,c->supporting_entity,c->state.ground_normal,c->state.ground_velocity};
        }
    }
#else
    (void)physics;
#endif
    result.rigid_box = entity->rigid_box;
    if(const auto owner=world.find(persistent_id))result.material=world.material_surface(*owner);
#if defined(VICE_RUNTIME_AUTHORED_PHYSICS)
    if(physics)if(const auto body=physics->body(persistent_id))result.native_mass=body->mass;
#endif
    result.distance_link = entity->distance_link;
    result.dependency = world.dependency_state(*world.find(persistent_id));
    result.camera = entity->camera.has_value();
    result.light = entity->light.has_value();
    result.light_enabled = entity->light && entity->light->enabled;
    result.light_supplied = result.light_enabled && world.dependency_available(*world.find(persistent_id));
    result.reflection_probe = entity->reflection_probe.has_value();
    result.resource_reference_count = entity->resource_references.size();

    if (entity->spatial_state.has_value() && entity->spatial_state->region.valid()) {
        const auto region = std::find_if(
            snapshot.regions.begin(),
            snapshot.regions.end(),
            [&](const world::RegionDesc& candidate) {
                return candidate.id == entity->spatial_state->region;
            }
        );
        if (region != snapshot.regions.end()) {
            result.region = RuntimeDebugRegionSnapshot{
                .id = region->id,
                .simulation_island = region->simulation_island,
                .origin = region->origin,
                .streaming_cell = region->streaming_cell,
                .fidelity = region->fidelity,
            };
        }
    }

    for (const auto& relation : snapshot.relations) {
        if (relation.subject == persistent_id) ++result.outgoing_relation_count;
        if (relation.object == persistent_id) ++result.incoming_relation_count;
    }
    return result;
}

std::string serialize_runtime_debug_entity_snapshot(
    const RuntimeDebugEntitySnapshot& snapshot,
    const std::string_view session_id
) {
    std::string output;
    output.reserve(2048U);
    output += "{\"schema\":\"vice.dev.runtime.debug.v1\",\"session_id\":";
    append_json_string(output, session_id);
    output += ",\"state\":";
    append_json_string(output, debug_state_name(snapshot.state));
    output += ",\"entity_id\":";
    if (snapshot.requested_id.is_nil()) {
        output += "null";
    } else {
        append_json_string(output, snapshot.requested_id.to_string());
    }

    output += ",\"causal_history\":"+world::serialize_causal_context(snapshot.causal_history);
    output += ",\"material\":"+world::serialize_material_surface(snapshot.material);
    output += ",\"native_mass\":";
    if(snapshot.native_mass)append_float(output,*snapshot.native_mass);else output+="null";
    output += ",\"spatial\":";
    if (!snapshot.spatial.has_value()) {
        output += "null";
    } else {
        output += "{\"global_position\":[";
        append_double(output, snapshot.spatial->global_position.x);
        output.push_back(',');
        append_double(output, snapshot.spatial->global_position.y);
        output.push_back(',');
        append_double(output, snapshot.spatial->global_position.z);
        output += "],\"region\":";
        if (snapshot.spatial->region.valid()) {
            output += std::to_string(snapshot.spatial->region.value);
        } else {
            output += "null";
        }
        output.push_back('}');
    }

    output += ",\"authored_transform\":";
    if (!snapshot.authored_transform.has_value()) {
        output += "null";
    } else {
        const auto& transform = *snapshot.authored_transform;
        output += "{\"rotation\":[";
        append_float(output, transform.rotation.x);
        output.push_back(',');
        append_float(output, transform.rotation.y);
        output.push_back(',');
        append_float(output, transform.rotation.z);
        output.push_back(',');
        append_float(output, transform.rotation.w);
        output += "],\"scale\":[";
        append_float(output, transform.scale.x);
        output.push_back(',');
        append_float(output, transform.scale.y);
        output.push_back(',');
        append_float(output, transform.scale.z);
        output += "]}";
    }

    output += ",\"character_attachment\":" + world::serialize_character_attachment(snapshot.character_attachment);
    output += ",\"animation\":"+world::serialize_animation(snapshot.animation)+",\"rig_part\":"+world::serialize_rig_part(snapshot.rig_part)+",\"audio_emitter\":"+world::serialize_audio_emitter(snapshot.audio_emitter)+",\"animation_state\":"+snapshot.animation_state+",\"audio_state\":"+snapshot.audio_state;
    output += ",\"vehicle\":" + world::serialize_vehicle(snapshot.vehicle);
    output += ",\"vehicle_state\":";
    if(!snapshot.vehicle_state)output+="null";
    else {
        const auto& v=*snapshot.vehicle_state;
        output+="{\"forward_speed\":";append_float(output,v.forward_speed);output+=",\"wheels\":[";
        for(std::size_t i=0;i<4;++i) {
            if(i)output+=',';
            const auto& w=v.wheels[i];
            output+="{\"grounded\":";output+=w.grounded?"true":"false";
            output+=",\"supporting_entity\":";append_json_string(output,w.supporting_entity.to_string());
            output+=",\"material\":";append_json_string(output,w.material.to_string());
            output+=",\"point\":[";append_float(output,w.point.x);output+=',';append_float(output,w.point.y);output+=',';append_float(output,w.point.z);output+=']';
            output+=",\"normal\":[";append_float(output,w.normal.x);output+=',';append_float(output,w.normal.y);output+=',';append_float(output,w.normal.z);output+=']';
            output+=",\"suspension_length\":";append_float(output,w.suspension_length);
            output+=",\"normal_force\":";append_float(output,w.normal_force);
            output+=",\"longitudinal_force\":";append_float(output,w.longitudinal_force);
            output+=",\"lateral_force\":";append_float(output,w.lateral_force);output+='}';
        }
        output+="]}";
    }
    output += ",\"character\":" + world::serialize_character(snapshot.character);
    output += ",\"character_state\":";
    if (!snapshot.character_state) output += "null";
    else {
        const auto& c = *snapshot.character_state;
        output += "{\"ground_state\":"; append_json_string(output,c.ground_state);
        output += ",\"supporting_entity\":";
        if (c.supporting_entity.is_nil()) output += "null"; else append_json_string(output,c.supporting_entity.to_string());
        const auto vector = [&](math::Vec3 v) { output += '['; append_float(output,v.x); output += ','; append_float(output,v.y); output += ','; append_float(output,v.z); output += ']'; };
        output += ",\"ground_normal\":"; vector(c.ground_normal); output += ",\"ground_velocity\":"; vector(c.ground_velocity); output += '}';
    }
    output += ",\"rigid_box\":";
    if (!snapshot.rigid_box) output += "null";
    else {
        const auto& box = *snapshot.rigid_box;
        output += "{\"motion\":";
        append_json_string(output, box.motion == world::RigidMotion::static_body ? "static" :
            (box.motion == world::RigidMotion::kinematic_body ? "kinematic" : "dynamic"));
        auto vector = [&](std::string_view name, math::Vec3 v) {
            output += ",\"" + std::string{name} + "\":[";
            append_float(output, v.x); output += ','; append_float(output, v.y); output += ','; append_float(output, v.z);
            output += ']';
        };
        vector("half_extents", box.half_extents);
        vector("linear_velocity", box.linear_velocity);
        vector("angular_velocity", box.angular_velocity);
        output += ",\"friction\":"; append_float(output, box.friction);
        output += ",\"restitution\":"; append_float(output, box.restitution);
        output += ",\"continuous\":"; output += box.continuous ? "true" : "false";
        output += ",\"trigger\":"; output += box.trigger ? "true" : "false";
        output += '}';
    }
    output += ",\"distance_link\":";
    if (!snapshot.distance_link) output += "null";
    else {
        const auto& link = *snapshot.distance_link;
        output += "{\"first\":"; append_json_string(output, link.first.to_string());
        output += ",\"second\":"; append_json_string(output, link.second.to_string());
        output += ",\"min_distance\":"; append_float(output, link.min_distance);
        output += ",\"max_distance\":"; append_float(output, link.max_distance);
        output += ",\"frequency_hz\":"; append_float(output, link.frequency_hz);
        output += ",\"damping_ratio\":"; append_float(output, link.damping_ratio);
        output += ",\"break_threshold\":"; append_float(output, link.break_threshold);
        output += ",\"broken\":"; output += link.broken ? "true" : "false";
        output += ",\"last_break_impulse\":"; append_float(output, link.last_break_impulse);
        output += ",\"break_step\":" + std::to_string(link.break_step) + "}";
    }
    output += ",\"dependency\":" + world::serialize_dependency_state(snapshot.dependency);
    output += ",\"region\":";
    if (!snapshot.region.has_value()) {
        output += "null";
    } else {
        output += "{\"id\":" + std::to_string(snapshot.region->id.value);
        output += ",\"simulation_island\":" +
            std::to_string(snapshot.region->simulation_island.value);
        output += ",\"origin\":[";
        append_double(output, snapshot.region->origin.x);
        output.push_back(',');
        append_double(output, snapshot.region->origin.y);
        output.push_back(',');
        append_double(output, snapshot.region->origin.z);
        output += "],\"streaming_cell\":[" +
            std::to_string(snapshot.region->streaming_cell.x) + "," +
            std::to_string(snapshot.region->streaming_cell.y) + "," +
            std::to_string(snapshot.region->streaming_cell.z) + "]";
        output += ",\"fidelity\":";
        append_json_string(output, fidelity_name(snapshot.region->fidelity));
        output.push_back('}');
    }

    output += ",\"light_enabled\":"; output += snapshot.light_enabled ? "true" : "false";
    output += ",\"light_supplied\":"; output += snapshot.light_supplied ? "true" : "false";
    output += ",\"components\":{\"camera\":";
    output += snapshot.camera ? "true" : "false";
    output += ",\"light\":";
    output += snapshot.light ? "true" : "false";
    output += ",\"reflection_probe\":";
    output += snapshot.reflection_probe ? "true" : "false";
    output += "},\"resource_reference_count\":" +
        std::to_string(snapshot.resource_reference_count);
    output += ",\"relations\":{\"outgoing\":" +
        std::to_string(snapshot.outgoing_relation_count);
    output += ",\"incoming\":" + std::to_string(snapshot.incoming_relation_count) + "}}\n";
    return output;
}

} // namespace vice::runtime
