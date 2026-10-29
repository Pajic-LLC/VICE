#include <vice/world/snapshot_file.hpp>

#include <vice/world/world.hpp>
#include <vice/world/presentation_io.hpp>
#include <sstream>
#include <iomanip>
#include <locale>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <span>
#include <string>
#include <vector>
#include <unordered_map>

namespace vice::world {
namespace {

constexpr std::array<std::byte, world_snapshot_file_magic_size> world_magic{
    std::byte{'V'}, std::byte{'I'}, std::byte{'C'}, std::byte{'E'},
    std::byte{'W'}, std::byte{'R'}, std::byte{'L'}, std::byte{'D'},
};
constexpr std::size_t header_size = world_snapshot_file_magic_size +
    sizeof(std::uint32_t) * 6U + sizeof(std::uint64_t);
constexpr std::uint32_t max_component_types = 65'536U;
constexpr std::uint32_t max_resources = 1'000'000U;
constexpr std::uint32_t max_regions = 65'536U;
constexpr std::uint32_t max_entities = 4'000'000U;
constexpr std::uint32_t max_relations = 8'000'000U;
constexpr std::uint32_t max_resource_references_per_entity = 256U;
constexpr std::uint32_t max_name_bytes = 4'096U;
constexpr std::uint64_t fnv_offset_basis = 14'695'981'039'346'656'037ULL;
constexpr std::uint64_t fnv_prime = 1'099'511'628'211ULL;

[[nodiscard]] std::uint64_t fingerprint(const std::span<const std::byte> bytes) noexcept {
    std::uint64_t hash = fnv_offset_basis;
    for (const std::byte value : bytes) {
        hash ^= std::to_integer<std::uint8_t>(value);
        hash *= fnv_prime;
    }
    return hash;
}

void append_u8(std::vector<std::byte>& bytes, const std::uint8_t value) {
    bytes.push_back(static_cast<std::byte>(value));
}

void append_u32(std::vector<std::byte>& bytes, const std::uint32_t value) {
    for (std::size_t index = 0; index < sizeof(value); ++index) {
        bytes.push_back(static_cast<std::byte>((value >> (index * 8U)) & 0xFFU));
    }
}

void append_u64(std::vector<std::byte>& bytes, const std::uint64_t value) {
    for (std::size_t index = 0; index < sizeof(value); ++index) {
        bytes.push_back(static_cast<std::byte>((value >> (index * 8U)) & 0xFFU));
    }
}

void append_i64(std::vector<std::byte>& bytes, const std::int64_t value) {
    append_u64(bytes, std::bit_cast<std::uint64_t>(value));
}

void append_f32(std::vector<std::byte>& bytes, const float value) {
    append_u32(bytes, std::bit_cast<std::uint32_t>(value));
}

void append_f64(std::vector<std::byte>& bytes, const double value) {
    append_u64(bytes, std::bit_cast<std::uint64_t>(value));
}

void append_id(std::vector<std::byte>& bytes, const PersistentId& id) {
    for (const std::uint8_t value : id.bytes()) {
        append_u8(bytes, value);
    }
}

void append_string(std::vector<std::byte>& bytes, const std::string& value) {
    append_u32(bytes, static_cast<std::uint32_t>(value.size()));
    for (const char character : value) {
        bytes.push_back(static_cast<std::byte>(static_cast<unsigned char>(character)));
    }
}

class Reader final {
public:
    explicit Reader(const std::span<const std::byte> bytes) noexcept : bytes_{bytes} {}

    bool read_u8(std::uint8_t& value) noexcept {
        if (remaining() < 1U) return false;
        value = std::to_integer<std::uint8_t>(bytes_[position_]);
        ++position_;
        return true;
    }

    bool read_u32(std::uint32_t& value) noexcept {
        if (remaining() < sizeof(value)) return false;
        value = 0;
        for (std::size_t index = 0; index < sizeof(value); ++index) {
            value |= static_cast<std::uint32_t>(
                std::to_integer<std::uint8_t>(bytes_[position_ + index])
            ) << (index * 8U);
        }
        position_ += sizeof(value);
        return true;
    }

    bool read_u64(std::uint64_t& value) noexcept {
        if (remaining() < sizeof(value)) return false;
        value = 0;
        for (std::size_t index = 0; index < sizeof(value); ++index) {
            value |= static_cast<std::uint64_t>(
                std::to_integer<std::uint8_t>(bytes_[position_ + index])
            ) << (index * 8U);
        }
        position_ += sizeof(value);
        return true;
    }

    bool read_i64(std::int64_t& value) noexcept {
        std::uint64_t bits = 0;
        if (!read_u64(bits)) return false;
        value = std::bit_cast<std::int64_t>(bits);
        return true;
    }

    bool read_f32(float& value) noexcept {
        std::uint32_t bits = 0;
        if (!read_u32(bits)) return false;
        value = std::bit_cast<float>(bits);
        return true;
    }

    bool read_f64(double& value) noexcept {
        std::uint64_t bits = 0;
        if (!read_u64(bits)) return false;
        value = std::bit_cast<double>(bits);
        return true;
    }

    bool read_id(PersistentId& id) noexcept {
        PersistentId::Bytes bytes{};
        for (std::uint8_t& value : bytes) {
            if (!read_u8(value)) return false;
        }
        id = PersistentId{bytes};
        return true;
    }

    bool read_string(std::string& value,std::size_t limit=max_name_bytes) {
        std::uint32_t length = 0;
        if (!read_u32(length) || length > limit || remaining() < length) {
            return false;
        }
        value.clear();
        value.reserve(length);
        for (std::uint32_t index = 0; index < length; ++index) {
            value.push_back(static_cast<char>(std::to_integer<unsigned char>(bytes_[position_])));
            ++position_;
        }
        return true;
    }

    [[nodiscard]] std::size_t remaining() const noexcept {
        return bytes_.size() - position_;
    }

    [[nodiscard]] bool finished() const noexcept {
        return position_ == bytes_.size();
    }

private:
    std::span<const std::byte> bytes_;
    std::size_t position_ = 0;
};

[[nodiscard]] bool count_fits(const std::size_t value) noexcept {
    return value <= static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max());
}

} // namespace

WorldSnapshotEncodeResult encode_world_snapshot(const WorldSnapshot& snapshot_data) {
    WorldSnapshotEncodeResult result;

    World canonical_world;
    if (!canonical_world.restore(snapshot_data)) {
        result.diagnostics = "World snapshot is not valid and cannot be encoded.";
        return result;
    }
    const WorldSnapshot snapshot = canonical_world.snapshot();

    if (!count_fits(snapshot.component_types.size()) ||
        !count_fits(snapshot.resources.size()) ||
        !count_fits(snapshot.regions.size()) ||
        !count_fits(snapshot.entities.size()) ||
        !count_fits(snapshot.relations.size())) {
        result.diagnostics = "World snapshot contains more records than the file format can represent.";
        return result;
    }
    for (const auto& type : snapshot.component_types) {
        if (type.name.size() > max_name_bytes) {
            result.diagnostics = "World snapshot contains a component type name that is too long.";
            return result;
        }
    }
    for (const auto& entity : snapshot.entities) {
        if (entity.resource_references.size() > max_resource_references_per_entity) {
            result.diagnostics = "World snapshot contains too many resource references on one entity.";
            return result;
        }
    }

    std::vector<std::byte> payload;
    for (const auto& type : snapshot.component_types) {
        append_id(payload, type.id.value);
        append_u32(payload, type.version);
        append_string(payload, type.name);
        std::ostringstream fields;fields.imbue(std::locale::classic());write_component_fields(fields,type.fields);append_string(payload,fields.str());
    }
    for (const ResourceId resource : snapshot.resources) {
        append_id(payload, resource.value);
    }
    for (const auto& region : snapshot.regions) {
        append_u64(payload, region.id.value);
        append_u64(payload, region.simulation_island.value);
        append_f64(payload, region.origin.x);
        append_f64(payload, region.origin.y);
        append_f64(payload, region.origin.z);
        append_i64(payload, region.streaming_cell.x);
        append_i64(payload, region.streaming_cell.y);
        append_i64(payload, region.streaming_cell.z);
        append_u8(payload, static_cast<std::uint8_t>(region.fidelity));
        append_u8(payload, region.bounds.has_value() ? 1U : 0U);
        if (region.bounds.has_value()) {
            append_f32(payload, region.bounds->min.x);
            append_f32(payload, region.bounds->min.y);
            append_f32(payload, region.bounds->min.z);
            append_f32(payload, region.bounds->max.x);
            append_f32(payload, region.bounds->max.y);
            append_f32(payload, region.bounds->max.z);
        }
    }
    for (const auto& entity : snapshot.entities) {
        append_id(payload, entity.persistent_id);
        append_u8(payload, entity.spatial_state.has_value() ? 1U : 0U);
        if (entity.spatial_state.has_value()) {
            append_f64(payload, entity.spatial_state->global_position.x);
            append_f64(payload, entity.spatial_state->global_position.y);
            append_f64(payload, entity.spatial_state->global_position.z);
            append_u64(payload, entity.spatial_state->region.value);
        }
        append_u8(payload, entity.authored_transform.has_value() ? 1U : 0U);
        if (entity.authored_transform.has_value()) {
            append_f32(payload, entity.authored_transform->rotation.x);
            append_f32(payload, entity.authored_transform->rotation.y);
            append_f32(payload, entity.authored_transform->rotation.z);
            append_f32(payload, entity.authored_transform->rotation.w);
            append_f32(payload, entity.authored_transform->scale.x);
            append_f32(payload, entity.authored_transform->scale.y);
            append_f32(payload, entity.authored_transform->scale.z);
        }
        append_u8(payload, entity.camera.has_value() ? 1U : 0U);
        if (entity.camera.has_value()) {
            append_u8(payload, static_cast<std::uint8_t>(entity.camera->projection));
            append_f32(payload, entity.camera->vertical_fov_radians);
            append_f32(payload, entity.camera->near_plane);
            append_f32(payload, entity.camera->far_plane);
            append_u8(payload, entity.camera->primary ? 1U : 0U);
        }
        append_u8(payload, entity.light.has_value() ? 1U : 0U);
        if (entity.light.has_value()) {
            append_u8(payload, static_cast<std::uint8_t>(entity.light->type));
            append_f32(payload, entity.light->color.red);
            append_f32(payload, entity.light->color.green);
            append_f32(payload, entity.light->color.blue);
            append_f32(payload, entity.light->intensity);
            append_f32(payload, entity.light->range);
            append_u8(payload, entity.light->enabled ? 1U : 0U);
        }
        append_u8(payload, entity.reflection_probe.has_value() ? 1U : 0U);
        if (entity.reflection_probe.has_value()) {
            append_f32(payload, entity.reflection_probe->half_extents.x);
            append_f32(payload, entity.reflection_probe->half_extents.y);
            append_f32(payload, entity.reflection_probe->half_extents.z);
            append_f32(payload, entity.reflection_probe->blend_distance);
            append_u32(payload, std::bit_cast<std::uint32_t>(entity.reflection_probe->priority));
            append_u8(payload, entity.reflection_probe->enabled ? 1U : 0U);
        }
        append_u8(payload, entity.rigid_box.has_value() ? 1U : 0U);
        if (entity.rigid_box) {
            const auto& box = *entity.rigid_box;
            append_u8(payload, static_cast<std::uint8_t>(box.motion));
            for (const auto vector : {box.half_extents, box.linear_velocity, box.angular_velocity}) {
                append_f32(payload, vector.x); append_f32(payload, vector.y); append_f32(payload, vector.z);
            }
            append_f32(payload, box.friction); append_f32(payload, box.restitution);
            append_u8(payload, box.continuous ? 1U : 0U);
            append_u8(payload, box.trigger ? 1U : 0U);
        }
        append_u8(payload, entity.distance_link ? 1U : 0U);
        if (entity.distance_link) {
            const auto& link = *entity.distance_link;
            append_id(payload, link.first); append_id(payload, link.second);
            for (const auto value : {link.min_distance, link.max_distance, link.frequency_hz, link.damping_ratio, link.break_threshold}) append_f32(payload, value);
            append_u8(payload, link.broken ? 1U : 0U);
            append_f32(payload, link.last_break_impulse); append_u64(payload, link.break_step);
        }
        append_u8(payload, entity.dependency ? 1U : 0U);
        if (entity.dependency) {
            append_id(payload, entity.dependency->provider); append_id(payload, entity.dependency->gate);
            append_u8(payload, entity.dependency->enabled ? 1U : 0U);
        }
        append_u8(payload, entity.character ? 1U : 0U);
        if (entity.character) {
            const auto& c = *entity.character;
            for (const auto x : {c.height,c.radius,c.mass,c.max_strength,c.max_slope_angle,c.step_height,c.stick_to_floor_distance,
                c.velocity.x,c.velocity.y,c.velocity.z,c.horizontal_velocity.x,c.horizontal_velocity.y,c.horizontal_velocity.z}) append_f32(payload,x);
            append_u8(payload,c.pending_jump_speed ? 1U : 0U);
            if (c.pending_jump_speed) append_f32(payload,*c.pending_jump_speed);
        }
        append_u32(payload, static_cast<std::uint32_t>(entity.resource_references.size()));
        for (const auto reference : entity.resource_references) {
            append_u8(payload, static_cast<std::uint8_t>(reference.role));
            append_id(payload, reference.resource.value);
        }
    }
    for (const auto& relation : snapshot.relations) {
        append_id(payload, relation.subject);
        append_id(payload, relation.kind);
        append_id(payload, relation.object);
    }

    const auto& history=snapshot.causal_history;
    append_u8(payload,history.id.is_nil()?0U:1U);
    if(!history.id.is_nil()) {
        append_id(payload,history.id);append_u64(payload,history.next_sequence);append_u32(payload,static_cast<std::uint32_t>(history.events.size()));
        for(const auto& e:history.events) {
            append_id(payload,e.id);append_id(payload,e.parent_id);append_id(payload,e.capture_id);append_u64(payload,e.sequence);append_u64(payload,e.parent_sequence);
            append_u8(payload,static_cast<std::uint8_t>(e.kind));append_u8(payload,static_cast<std::uint8_t>(e.origin));
            for(const auto id:{e.subject,e.first,e.second,e.root_entity})append_id(payload,id);
            append_u8(payload,e.value?1U:0U);append_u8(payload,static_cast<std::uint8_t>(e.reason));append_u8(payload,static_cast<std::uint8_t>(e.root_reason));
            append_u8(payload,static_cast<std::uint8_t>(e.quantity_kind));append_f32(payload,e.quantity);append_u64(payload,e.solver_step);append_u64(payload,e.region.value);
            append_id(payload,e.first_material);append_id(payload,e.second_material);
        }
    }
    append_u32(payload,static_cast<std::uint32_t>(snapshot.materials.size()));
    for(const auto& m:snapshot.materials) {
        append_id(payload,m.id);append_string(payload,m.name);append_f32(payload,m.density);
        append_f32(payload,m.friction);append_f32(payload,m.restitution);append_id(payload,m.render_material.value);
    }
    const auto bindings=std::count_if(snapshot.entities.begin(),snapshot.entities.end(),[](const auto& e){return !e.material.is_nil();});
    append_u32(payload,static_cast<std::uint32_t>(bindings));
    for(const auto& e:snapshot.entities) if(!e.material.is_nil()) {append_id(payload,e.persistent_id);append_id(payload,e.material);}
    const auto vehicles=std::count_if(snapshot.entities.begin(),snapshot.entities.end(),[](const auto& e){return e.vehicle.has_value();});
    append_u32(payload,static_cast<std::uint32_t>(vehicles));
    for(const auto& e:snapshot.entities) if(e.vehicle) {
        append_id(payload,e.persistent_id); const auto& v=*e.vehicle;
        for(const auto x:{v.wheelbase,v.track,v.mount_height,v.wheel_radius,v.suspension_travel,v.spring,v.damping,v.drive_force,
            v.brake_force,v.lateral_stiffness,v.max_steer,v.tire_friction,v.throttle,v.steering,v.brake})append_f32(payload,x);
        append_f64(payload,v.distance);
    }
    const auto attachments=std::count_if(snapshot.entities.begin(),snapshot.entities.end(),[](const auto& e){return e.character_attachment.has_value();});
    append_u32(payload,static_cast<std::uint32_t>(attachments));
    for(const auto& e:snapshot.entities)if(e.character_attachment){append_id(payload,e.persistent_id);const auto& a=*e.character_attachment;
        append_id(payload,a.carrier);append_f32(payload,a.offset.x);append_f32(payload,a.offset.y);append_f32(payload,a.offset.z);}
    const auto presentations=std::count_if(snapshot.entities.begin(),snapshot.entities.end(),[](const auto& e){return e.animation||e.rig_part||e.audio_emitter;});
    append_u32(payload,static_cast<std::uint32_t>(presentations));
    for(const auto& e:snapshot.entities)if(e.animation||e.rig_part||e.audio_emitter){
        append_id(payload,e.persistent_id);std::ostringstream o;o.imbue(std::locale::classic());o<<std::setprecision(17);
        if(e.animation){o<<"animation ";write_animation(o,*e.animation);o<<' ';}if(e.rig_part){o<<"rig_part ";write_rig_part(o,*e.rig_part);o<<' ';}if(e.audio_emitter){o<<"audio_emitter ";write_audio_emitter(o,*e.audio_emitter);o<<' ';}append_string(payload,o.str());
    }
    std::size_t component_count=0;for(const auto& e:snapshot.entities)component_count+=e.components.size();
    append_u32(payload,static_cast<std::uint32_t>(component_count));
    for(const auto& e:snapshot.entities)for(const auto& c:e.components){append_id(payload,e.persistent_id);std::ostringstream data;data.imbue(std::locale::classic());write_component_payload(data,c);append_string(payload,data.str());}
    result.bytes.reserve(header_size + payload.size());
    result.bytes.insert(result.bytes.end(), world_magic.begin(), world_magic.end());
    append_u32(result.bytes, world_snapshot_file_format_version);
    append_u32(result.bytes, static_cast<std::uint32_t>(snapshot.component_types.size()));
    append_u32(result.bytes, static_cast<std::uint32_t>(snapshot.resources.size()));
    append_u32(result.bytes, static_cast<std::uint32_t>(snapshot.regions.size()));
    append_u32(result.bytes, static_cast<std::uint32_t>(snapshot.entities.size()));
    append_u32(result.bytes, static_cast<std::uint32_t>(snapshot.relations.size()));
    append_u64(result.bytes, fingerprint(payload));
    result.bytes.insert(result.bytes.end(), payload.begin(), payload.end());
    return result;
}

WorldSnapshotDecodeResult decode_world_snapshot(const std::span<const std::byte> bytes) {
    WorldSnapshotDecodeResult result;
    if (bytes.size() < header_size) {
        result.diagnostics = "World snapshot file is smaller than the VICEWRLD header.";
        return result;
    }
    if (!std::equal(world_magic.begin(), world_magic.end(), bytes.begin())) {
        result.diagnostics = "World snapshot file magic is invalid.";
        return result;
    }

    Reader reader{bytes.subspan(world_snapshot_file_magic_size)};
    std::uint32_t version = 0;
    std::uint32_t component_count = 0;
    std::uint32_t resource_count = 0;
    std::uint32_t region_count = 0;
    std::uint32_t entity_count = 0;
    std::uint32_t relation_count = 0;
    std::uint64_t stored_fingerprint = 0;
    if (!reader.read_u32(version) ||
        !reader.read_u32(component_count) ||
        !reader.read_u32(resource_count) ||
        !reader.read_u32(region_count) ||
        !reader.read_u32(entity_count) ||
        !reader.read_u32(relation_count) ||
        !reader.read_u64(stored_fingerprint)) {
        result.diagnostics = "World snapshot header is incomplete.";
        return result;
    }
    if (version < world_snapshot_file_min_supported_version ||
        version > world_snapshot_file_format_version) {
        result.diagnostics = "World snapshot file version is not supported.";
        return result;
    }
    if (component_count > max_component_types || resource_count > max_resources ||
        region_count > max_regions || entity_count > max_entities ||
        relation_count > max_relations) {
        result.diagnostics = "World snapshot file declares an unsafe record count.";
        return result;
    }
    if (fingerprint(bytes.subspan(header_size)) != stored_fingerprint) {
        result.diagnostics = "World snapshot payload fingerprint does not match.";
        return result;
    }

    WorldSnapshot snapshot;
    snapshot.component_types.reserve(component_count);
    snapshot.resources.reserve(resource_count);
    snapshot.regions.reserve(region_count);
    snapshot.entities.reserve(entity_count);
    snapshot.relations.reserve(relation_count);

    for (std::uint32_t index = 0; index < component_count; ++index) {
        ComponentTypeDesc type;
        if (!reader.read_id(type.id.value) || !reader.read_u32(type.version) ||
            !reader.read_string(type.name)) {
            result.diagnostics = "World snapshot ended inside a component type record.";
            return result;
        }
        if(version>=16U){std::string data;if(!reader.read_string(data)){result.diagnostics="Truncated component schema.";return result;}std::istringstream fields{data};if(!read_component_fields(fields,type.fields)||(fields>>std::ws,!fields.eof())){result.diagnostics="Invalid component schema.";return result;}}
        snapshot.component_types.push_back(std::move(type));
    }
    for (std::uint32_t index = 0; index < resource_count; ++index) {
        ResourceId resource;
        if (!reader.read_id(resource.value)) {
            result.diagnostics = "World snapshot ended inside a resource record.";
            return result;
        }
        snapshot.resources.push_back(resource);
    }
    for (std::uint32_t index = 0; index < region_count; ++index) {
        RegionDesc region;
        std::uint8_t fidelity = 0;
        std::uint8_t has_bounds = 0;
        if (!reader.read_u64(region.id.value) ||
            !reader.read_u64(region.simulation_island.value) ||
            !reader.read_f64(region.origin.x) ||
            !reader.read_f64(region.origin.y) ||
            !reader.read_f64(region.origin.z) ||
            !reader.read_i64(region.streaming_cell.x) ||
            !reader.read_i64(region.streaming_cell.y) ||
            !reader.read_i64(region.streaming_cell.z) ||
            !reader.read_u8(fidelity) ||
            !reader.read_u8(has_bounds)) {
            result.diagnostics = "World snapshot ended inside a region record.";
            return result;
        }
        if (fidelity > static_cast<std::uint8_t>(RegionFidelity::full) || has_bounds > 1U) {
            result.diagnostics = "World snapshot contains invalid region metadata.";
            return result;
        }
        region.fidelity = static_cast<RegionFidelity>(fidelity);
        if (has_bounds != 0U) {
            RegionBounds bounds;
            if (!reader.read_f32(bounds.min.x) ||
                !reader.read_f32(bounds.min.y) ||
                !reader.read_f32(bounds.min.z) ||
                !reader.read_f32(bounds.max.x) ||
                !reader.read_f32(bounds.max.y) ||
                !reader.read_f32(bounds.max.z)) {
                result.diagnostics = "World snapshot ended inside region bounds.";
                return result;
            }
            region.bounds = bounds;
        }
        snapshot.regions.push_back(region);
    }
    for (std::uint32_t index = 0; index < entity_count; ++index) {
        SnapshotEntity entity;
        std::uint8_t has_spatial = 0;
        if (!reader.read_id(entity.persistent_id) || !reader.read_u8(has_spatial)) {
            result.diagnostics = "World snapshot ended inside an entity record.";
            return result;
        }
        if (has_spatial > 1U) {
            result.diagnostics = "World snapshot contains invalid entity metadata.";
            return result;
        }
        if (has_spatial != 0U) {
            SpatialState spatial;
            if (!reader.read_f64(spatial.global_position.x) ||
                !reader.read_f64(spatial.global_position.y) ||
                !reader.read_f64(spatial.global_position.z) ||
                !reader.read_u64(spatial.region.value)) {
                result.diagnostics = "World snapshot ended inside entity spatial state.";
                return result;
            }
            entity.spatial_state = spatial;
        }

        if (version >= 2U) {
            std::uint8_t has_authored_transform = 0;
            if (!reader.read_u8(has_authored_transform)) {
                result.diagnostics = "World snapshot ended inside entity authored transform metadata.";
                return result;
            }
            if (has_authored_transform > 1U) {
                result.diagnostics = "World snapshot contains invalid entity authored transform metadata.";
                return result;
            }
            if (has_authored_transform != 0U) {
                AuthoredTransform transform;
                if (!reader.read_f32(transform.rotation.x) ||
                    !reader.read_f32(transform.rotation.y) ||
                    !reader.read_f32(transform.rotation.z) ||
                    !reader.read_f32(transform.rotation.w) ||
                    !reader.read_f32(transform.scale.x) ||
                    !reader.read_f32(transform.scale.y) ||
                    !reader.read_f32(transform.scale.z)) {
                    result.diagnostics = "World snapshot ended inside entity authored transform.";
                    return result;
                }
                entity.authored_transform = transform;
            }
        }

        if (version >= 4U) {
            std::uint8_t has_camera = 0;
            if (!reader.read_u8(has_camera)) {
                result.diagnostics = "World snapshot ended inside entity camera metadata.";
                return result;
            }
            if (has_camera > 1U) {
                result.diagnostics = "World snapshot contains invalid entity camera metadata.";
                return result;
            }
            if (has_camera != 0U) {
                std::uint8_t projection = 0;
                std::uint8_t primary = 0;
                Camera camera;
                if (!reader.read_u8(projection) ||
                    !reader.read_f32(camera.vertical_fov_radians) ||
                    !reader.read_f32(camera.near_plane) ||
                    !reader.read_f32(camera.far_plane) ||
                    !reader.read_u8(primary)) {
                    result.diagnostics = "World snapshot ended inside entity camera record.";
                    return result;
                }
                camera.projection = static_cast<CameraProjection>(projection);
                camera.primary = primary != 0U;
                if (primary > 1U || !valid(camera)) {
                    result.diagnostics = "World snapshot contains an invalid entity camera record.";
                    return result;
                }
                entity.camera = camera;
            }
        }

        if (version >= 5U) {
            std::uint8_t has_light = 0;
            if (!reader.read_u8(has_light)) {
                result.diagnostics = "World snapshot ended inside entity light metadata.";
                return result;
            }
            if (has_light > 1U) {
                result.diagnostics = "World snapshot contains invalid entity light metadata.";
                return result;
            }
            if (has_light != 0U) {
                std::uint8_t type = 0;
                std::uint8_t enabled = 0;
                Light light;
                if (!reader.read_u8(type) ||
                    !reader.read_f32(light.color.red) ||
                    !reader.read_f32(light.color.green) ||
                    !reader.read_f32(light.color.blue) ||
                    !reader.read_f32(light.intensity) ||
                    !reader.read_f32(light.range) ||
                    !reader.read_u8(enabled)) {
                    result.diagnostics = "World snapshot ended inside entity light record.";
                    return result;
                }
                light.type = static_cast<LightType>(type);
                light.enabled = enabled != 0U;
                if (enabled > 1U || !valid(light)) {
                    result.diagnostics = "World snapshot contains an invalid entity light record.";
                    return result;
                }
                entity.light = light;
            }
        }

        if (version >= 6U) {
            std::uint8_t has_probe = 0U;
            if (!reader.read_u8(has_probe) || has_probe > 1U) {
                result.diagnostics = "World snapshot contains invalid reflection probe metadata.";
                return result;
            }
            if (has_probe != 0U) {
                ReflectionProbe probe;
                std::uint32_t priority_bits = 0U;
                std::uint8_t enabled = 0U;
                if (!reader.read_f32(probe.half_extents.x) ||
                    !reader.read_f32(probe.half_extents.y) ||
                    !reader.read_f32(probe.half_extents.z) ||
                    !reader.read_f32(probe.blend_distance) ||
                    !reader.read_u32(priority_bits) || !reader.read_u8(enabled)) {
                    result.diagnostics = "World snapshot ended inside entity reflection probe record.";
                    return result;
                }
                probe.priority = std::bit_cast<std::int32_t>(priority_bits);
                probe.enabled = enabled != 0U;
                if (enabled > 1U || !valid(probe)) {
                    result.diagnostics = "World snapshot contains an invalid reflection probe record.";
                    return result;
                }
                entity.reflection_probe = probe;
            }
        }

        if (version >= 7U) {
            std::uint8_t present = 0;
            if (!reader.read_u8(present) || present > 1U) {
                result.diagnostics = "Invalid rigid box presence flag.";
                return result;
            }
            if (present != 0U) {
                RigidBox box;
                std::uint8_t motion = 0, continuous = 0, trigger = 0;
                if (!reader.read_u8(motion) ||
                    !reader.read_f32(box.half_extents.x) || !reader.read_f32(box.half_extents.y) || !reader.read_f32(box.half_extents.z) ||
                    !reader.read_f32(box.linear_velocity.x) || !reader.read_f32(box.linear_velocity.y) || !reader.read_f32(box.linear_velocity.z) ||
                    !reader.read_f32(box.angular_velocity.x) || !reader.read_f32(box.angular_velocity.y) || !reader.read_f32(box.angular_velocity.z) ||
                    !reader.read_f32(box.friction) || !reader.read_f32(box.restitution) ||
                    !reader.read_u8(continuous) || !reader.read_u8(trigger)) {
                    result.diagnostics = "World snapshot ended inside rigid box data.";
                    return result;
                }
                box.motion = static_cast<RigidMotion>(motion);
                box.continuous = continuous != 0U;
                box.trigger = trigger != 0U;
                if (continuous > 1U || trigger > 1U || !valid(box)) {
                    result.diagnostics = "World snapshot contains invalid rigid box data.";
                    return result;
                }
                entity.rigid_box = box;
            }
        }
        if (version >= 8U) {
            std::uint8_t present = 0, broken = 0;
            DistanceLink link;
            if (!reader.read_u8(present) || present > 1U) { result.diagnostics = "Invalid distance link presence flag."; return result; }
            if (present) {
                if (!reader.read_id(link.first) || !reader.read_id(link.second) ||
                    !reader.read_f32(link.min_distance) || !reader.read_f32(link.max_distance) ||
                    !reader.read_f32(link.frequency_hz) || !reader.read_f32(link.damping_ratio) || !reader.read_f32(link.break_threshold) ||
                    !reader.read_u8(broken) || broken > 1U || !reader.read_f32(link.last_break_impulse) || !reader.read_u64(link.break_step)) {
                    result.diagnostics = "World snapshot ended inside invalid distance link data."; return result;
                }
                link.broken = broken != 0;
                if (!valid(link)) { result.diagnostics = "World snapshot contains invalid distance link data."; return result; }
                entity.distance_link = link;
            }
        }
        if (version >= 9U) {
            std::uint8_t present = 0, enabled = 0;
            Dependency d;
            if (!reader.read_u8(present) || present > 1U) { result.diagnostics = "Invalid dependency presence flag."; return result; }
            if (present) {
                if (!reader.read_id(d.provider) || !reader.read_id(d.gate) || !reader.read_u8(enabled) || enabled > 1U) {
                    result.diagnostics = "Invalid dependency record."; return result;
                }
                d.enabled = enabled != 0; entity.dependency = d;
            }
        }
        if (version >= 10U) {
            std::uint8_t present = 0, jumping = 0; Character c;
            if (!reader.read_u8(present) || present > 1U) { result.diagnostics = "Invalid character presence flag."; return result; }
            if (present) {
                for (auto* x : {&c.height,&c.radius,&c.mass,&c.max_strength,&c.max_slope_angle,&c.step_height,&c.stick_to_floor_distance,
                    &c.velocity.x,&c.velocity.y,&c.velocity.z,&c.horizontal_velocity.x,&c.horizontal_velocity.y,&c.horizontal_velocity.z}) {
                    if (!reader.read_f32(*x)) { result.diagnostics = "Truncated character record."; return result; }
                }
                if (!reader.read_u8(jumping) || jumping > 1U) { result.diagnostics = "Invalid character jump flag."; return result; }
                if (jumping) { float speed = 0; if (!reader.read_f32(speed)) { result.diagnostics = "Truncated character jump."; return result; } c.pending_jump_speed = speed; }
                if (!valid(c)) { result.diagnostics = "Invalid character settings or state."; return result; }
                entity.character = c;
            }
        }
        if (version >= 3U) {
            std::uint32_t reference_count = 0;
            if (!reader.read_u32(reference_count) ||
                reference_count > max_resource_references_per_entity) {
                result.diagnostics = "World snapshot contains invalid entity resource reference metadata.";
                return result;
            }
            entity.resource_references.reserve(reference_count);
            for (std::uint32_t reference_index = 0; reference_index < reference_count; ++reference_index) {
                std::uint8_t role_value = 0;
                ResourceId resource;
                if (!reader.read_u8(role_value) || !reader.read_id(resource.value)) {
                    result.diagnostics = "World snapshot ended inside an entity resource reference.";
                    return result;
                }
                const auto role = static_cast<ResourceRole>(role_value);
                if (!valid(role) || !resource.valid()) {
                    result.diagnostics = "World snapshot contains an invalid entity resource reference.";
                    return result;
                }
                entity.resource_references.push_back(ResourceReference{role, resource});
            }
        }
        snapshot.entities.push_back(std::move(entity));
    }
    for (std::uint32_t index = 0; index < relation_count; ++index) {
        SnapshotRelation relation;
        if (!reader.read_id(relation.subject) ||
            !reader.read_id(relation.kind) ||
            !reader.read_id(relation.object)) {
            result.diagnostics = "World snapshot ended inside a relation record.";
            return result;
        }
        snapshot.relations.push_back(relation);
    }

    if(version>=11U) {
        std::uint8_t present=0;
        if(!reader.read_u8(present) || present>1U){result.diagnostics="Invalid causal history presence.";return result;}
        if(present) {
            auto& h=snapshot.causal_history;std::uint32_t count=0;
            if(!reader.read_id(h.id) || h.id.is_nil() || !reader.read_u64(h.next_sequence) || !reader.read_u32(count) || count>causal_history_capacity) {
                result.diagnostics="Invalid causal history header.";return result;
            }
            h.events.reserve(count);
            for(std::uint32_t i=0;i<count;++i) {
                CausalEvent e;std::uint8_t kind=0,origin=0,value=0,reason=0,root_reason=0,quantity=0;
                if(!reader.read_id(e.id) || !reader.read_id(e.parent_id) || !reader.read_id(e.capture_id) || !reader.read_u64(e.sequence) || !reader.read_u64(e.parent_sequence) ||
                    !reader.read_u8(kind) || !reader.read_u8(origin) || !reader.read_id(e.subject) || !reader.read_id(e.first) || !reader.read_id(e.second) || !reader.read_id(e.root_entity) ||
                    !reader.read_u8(value) || value>1U || !reader.read_u8(reason) || !reader.read_u8(root_reason) || !reader.read_u8(quantity) ||
                    !reader.read_f32(e.quantity) || !reader.read_u64(e.solver_step) || !reader.read_u64(e.region.value)) {result.diagnostics="Truncated or invalid causal event.";return result;}
                e.kind=static_cast<CausalKind>(kind);e.origin=static_cast<CausalOrigin>(origin);e.value=value!=0;e.reason=static_cast<DependencyReason>(reason);
                e.root_reason=static_cast<DependencyReason>(root_reason);e.quantity_kind=static_cast<CausalQuantity>(quantity);
                if(version>=12U && (!reader.read_id(e.first_material) || !reader.read_id(e.second_material))) {result.diagnostics="Truncated causal material identities.";return result;}
                h.events.push_back(e);
            }
            if(!validate_causal_history(h,&result.diagnostics))return result;
        }
    }
    if(version>=12U) {
        std::uint32_t count=0;
        if(!reader.read_u32(count) || count>max_world_materials) {result.diagnostics="Invalid material catalog size.";return result;}
        snapshot.materials.reserve(count);
        for(std::uint32_t i=0;i<count;++i) {
            MaterialDefinition m;
            if(!reader.read_id(m.id) || !reader.read_string(m.name) || !reader.read_f32(m.density) || !reader.read_f32(m.friction) ||
               !reader.read_f32(m.restitution) || !reader.read_id(m.render_material.value)) {result.diagnostics="Truncated World material.";return result;}
            snapshot.materials.push_back(std::move(m));
        }
        if(!reader.read_u32(count) || count>snapshot.entities.size()) {result.diagnostics="Invalid material binding count.";return result;}
        std::unordered_map<PersistentId,PersistentId,PersistentIdHash> bindings;
        bindings.reserve(count);
        for(std::uint32_t i=0;i<count;++i) {
            PersistentId entity,material;
            if(!reader.read_id(entity) || !reader.read_id(material) || material.is_nil() || !bindings.emplace(entity,material).second) {
                result.diagnostics="Invalid or duplicate material binding.";return result;
            }
        }
        if(count)for(auto& e:snapshot.entities) {
            const auto found=bindings.find(e.persistent_id);
            if(found!=bindings.end()) {e.material=found->second;bindings.erase(found);}
        }
        if(!bindings.empty()) {result.diagnostics="Material binding entity is missing.";return result;}
        if(!validate_materials(snapshot,&result.diagnostics))return result;
    }
    if(version>=13U) {
        std::uint32_t count=0;
        if(!reader.read_u32(count) || count>snapshot.entities.size()) {result.diagnostics="Invalid vehicle count.";return result;}
        std::unordered_map<PersistentId,Vehicle,PersistentIdHash> vehicles;
        vehicles.reserve(count);
        for(std::uint32_t i=0;i<count;++i) {
            PersistentId id;Vehicle v;
            if(!reader.read_id(id)) {result.diagnostics="Truncated vehicle identity.";return result;}
            for(auto* x:{&v.wheelbase,&v.track,&v.mount_height,&v.wheel_radius,&v.suspension_travel,&v.spring,&v.damping,&v.drive_force,
                &v.brake_force,&v.lateral_stiffness,&v.max_steer,&v.tire_friction,&v.throttle,&v.steering,&v.brake}) {
                if(!reader.read_f32(*x)){result.diagnostics="Truncated vehicle profile.";return result;}
            }
            if(!reader.read_f64(v.distance) || !valid(v) || !vehicles.emplace(id,v).second){result.diagnostics="Invalid or duplicate vehicle.";return result;}
        }
        if(count)for(auto& e:snapshot.entities) {
            const auto found=vehicles.find(e.persistent_id);
            if(found!=vehicles.end()){e.vehicle=found->second;vehicles.erase(found);}
        }
        if(!vehicles.empty()){result.diagnostics="Vehicle entity is missing.";return result;}
        if(!validate_vehicles(snapshot,&result.diagnostics))return result;
    }
    if(version>=14U) {
        std::uint32_t count=0;
        if(!reader.read_u32(count) || count>snapshot.entities.size()){result.diagnostics="Invalid character attachment count.";return result;}
        std::unordered_map<PersistentId,CharacterAttachment,PersistentIdHash> attachments;attachments.reserve(count);
        for(std::uint32_t i=0;i<count;++i){PersistentId id;CharacterAttachment a;
            if(!reader.read_id(id) || !reader.read_id(a.carrier) || !reader.read_f32(a.offset.x) || !reader.read_f32(a.offset.y) || !reader.read_f32(a.offset.z) || !valid(a) || !attachments.emplace(id,a).second){result.diagnostics="Invalid or duplicate character attachment.";return result;}}
        if(count)for(auto& e:snapshot.entities){const auto found=attachments.find(e.persistent_id);if(found!=attachments.end()){e.character_attachment=found->second;attachments.erase(found);}}
        if(!attachments.empty() || !validate_character_attachments(snapshot,&result.diagnostics)){result.diagnostics="Invalid character attachment references.";return result;}
    }
    if(version>=15U){
        std::uint32_t count=0;if(!reader.read_u32(count)||count>snapshot.entities.size()){result.diagnostics="Invalid presentation count.";return result;}
        std::unordered_map<PersistentId,std::string,PersistentIdHash> records;
        for(std::uint32_t i=0;i<count;++i){PersistentId id;std::string data;if(!reader.read_id(id)||!reader.read_string(data)||data.empty()||!records.emplace(id,std::move(data)).second){result.diagnostics="Invalid presentation record.";return result;}}
        for(auto& e:snapshot.entities){const auto found=records.find(e.persistent_id);if(found==records.end())continue;std::istringstream in{found->second};in.imbue(std::locale::classic());std::string field;
            while(in>>field){bool ok=false;if(field=="animation"&&!e.animation){AnimationPlayer p;ok=read_animation(in,p);if(ok)e.animation=p;}else if(field=="rig_part"&&!e.rig_part){RigPart p;ok=read_rig_part(in,p);if(ok)e.rig_part=p;}else if(field=="audio_emitter"&&!e.audio_emitter){AudioEmitter p;ok=read_audio_emitter(in,p);if(ok)e.audio_emitter=p;}if(!ok){result.diagnostics="Invalid presentation fields.";return result;}}
            records.erase(found);
        }if(!records.empty()||!validate_presentation(snapshot,&result.diagnostics)){result.diagnostics="Invalid presentation references.";return result;}
    }
    if(version>=16U){
        std::uint32_t count=0;if(!reader.read_u32(count)||count>max_world_component_payloads||count>snapshot.entities.size()*max_entity_components){result.diagnostics="Invalid component payload count.";return result;}
        std::unordered_map<PersistentId,std::size_t,PersistentIdHash> owners;for(std::size_t i=0;i<snapshot.entities.size();++i)owners.emplace(snapshot.entities[i].persistent_id,i);
        for(std::uint32_t i=0;i<count;++i){PersistentId owner;std::string data;ComponentPayload p;
            if(!reader.read_id(owner)||!reader.read_string(data,8192)){result.diagnostics="Truncated component payload.";return result;}
            const auto found=owners.find(owner);std::istringstream input{data};
            if(found==owners.end()||!read_component_payload(input,p)||(input>>std::ws,!input.eof())){result.diagnostics="Invalid component payload/owner.";return result;}
            auto& components=snapshot.entities[found->second].components;if(components.size()>=max_entity_components){result.diagnostics="Entity component capacity exceeded.";return result;}components.push_back(std::move(p));
        }
    }
    if (!reader.finished()) {
        result.diagnostics = "World snapshot contains trailing data.";
        return result;
    }

    World validated;
    if (!validated.restore(snapshot)) {
        result.diagnostics = "World snapshot payload is structurally valid but violates World rules.";
        return result;
    }
    result.snapshot = validated.snapshot();
    return result;
}

WorldSnapshotDecodeResult load_world_snapshot(const std::filesystem::path& path) {
    WorldSnapshotDecodeResult result;
    std::ifstream input{path, std::ios::binary};
    if (!input) {
        result.diagnostics = "Could not open cooked world snapshot: " + path.string();
        return result;
    }
    const std::vector<char> raw{
        std::istreambuf_iterator<char>{input},
        std::istreambuf_iterator<char>{},
    };
    if (!input.eof() && input.fail()) {
        result.diagnostics = "Could not read cooked world snapshot: " + path.string();
        return result;
    }
    return decode_world_snapshot(std::as_bytes(std::span<const char>{raw}));
}

bool save_world_snapshot(
    const std::filesystem::path& path,
    const WorldSnapshot& snapshot,
    std::string* diagnostics
) {
    const auto fail = [&](const std::string& message) {
        if (diagnostics != nullptr) *diagnostics = message;
        return false;
    };

    const auto encoded = encode_world_snapshot(snapshot);
    if (!encoded) {
        return fail(encoded.diagnostics);
    }

    std::error_code error;
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path(), error);
        if (error) {
            return fail("Could not create world snapshot directory: " + error.message());
        }
    }

    auto temporary_path = path;
    temporary_path += ".tmp";
    {
        std::ofstream output{temporary_path, std::ios::binary | std::ios::trunc};
        if (!output) {
            return fail("Could not open temporary world snapshot: " + temporary_path.string());
        }
        output.write(
            reinterpret_cast<const char*>(encoded.bytes.data()),
            static_cast<std::streamsize>(encoded.bytes.size())
        );
        if (!output) {
            std::filesystem::remove(temporary_path, error);
            return fail("Could not write temporary world snapshot: " + temporary_path.string());
        }
    }

    error.clear();
    if (std::filesystem::exists(path, error)) {
        if (error) {
            std::filesystem::remove(temporary_path, error);
            return fail("Could not inspect existing world snapshot: " + error.message());
        }
        std::filesystem::remove(path, error);
        if (error) {
            std::filesystem::remove(temporary_path, error);
            return fail("Could not replace existing world snapshot: " + error.message());
        }
    }

    error.clear();
    std::filesystem::rename(temporary_path, path, error);
    if (error) {
        return fail("Could not finalize world snapshot: " + error.message());
    }
    if (diagnostics != nullptr) diagnostics->clear();
    return true;
}

} // namespace vice::world
