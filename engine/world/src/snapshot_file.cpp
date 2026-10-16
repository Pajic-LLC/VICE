#include <vice/world/snapshot_file.hpp>

#include <vice/world/world.hpp>

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

    bool read_string(std::string& value) {
        std::uint32_t length = 0;
        if (!read_u32(length) || length > max_name_bytes || remaining() < length) {
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

    std::vector<std::byte> payload;
    for (const auto& type : snapshot.component_types) {
        append_id(payload, type.id.value);
        append_u32(payload, type.version);
        append_string(payload, type.name);
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
    }
    for (const auto& relation : snapshot.relations) {
        append_id(payload, relation.subject);
        append_id(payload, relation.kind);
        append_id(payload, relation.object);
    }

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
    if (version != world_snapshot_file_format_version) {
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
        snapshot.entities.push_back(entity);
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
