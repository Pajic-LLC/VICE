#include <vice/runtime/debug.hpp>

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
    const world::PersistentId persistent_id
) {
    RuntimeDebugEntitySnapshot result;
    result.requested_id = persistent_id;
    if (persistent_id.is_nil()) {
        result.state = RuntimeDebugEntityState::invalid_request;
        return result;
    }

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
    result.rigid_box = entity->rigid_box;
    result.camera = entity->camera.has_value();
    result.light = entity->light.has_value();
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
