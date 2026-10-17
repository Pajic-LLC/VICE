#include <vice/tools/world_cooker.hpp>
#include <vice/world/snapshot_file.hpp>
#include <vice/world/world.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string_view>
#include <vector>

namespace {

constexpr std::uint64_t fnv_offset_basis = 14'695'981'039'346'656'037ULL;
constexpr std::uint64_t fnv_prime = 1'099'511'628'211ULL;

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

void append_f64(std::vector<std::byte>& bytes, const double value) {
    append_u64(bytes, std::bit_cast<std::uint64_t>(value));
}

void append_id(std::vector<std::byte>& bytes, const vice::world::PersistentId& id) {
    for (const std::uint8_t value : id.bytes()) {
        append_u8(bytes, value);
    }
}

[[nodiscard]] std::uint64_t fingerprint(const std::span<const std::byte> bytes) noexcept {
    std::uint64_t hash = fnv_offset_basis;
    for (const std::byte value : bytes) {
        hash ^= std::to_integer<std::uint8_t>(value);
        hash *= fnv_prime;
    }
    return hash;
}

[[nodiscard]] std::vector<std::byte> legacy_v1_snapshot() {
    using namespace vice::world;

    const auto entity_id = PersistentId::parse("10000000-0000-4000-8000-000000000001");
    assert(entity_id.has_value());

    std::vector<std::byte> payload;
    append_u64(payload, 1U);
    append_u64(payload, 1U);
    append_f64(payload, 0.0);
    append_f64(payload, 0.0);
    append_f64(payload, 0.0);
    append_i64(payload, 0);
    append_i64(payload, 0);
    append_i64(payload, 0);
    append_u8(payload, static_cast<std::uint8_t>(RegionFidelity::full));
    append_u8(payload, 0U);

    append_id(payload, *entity_id);
    append_u8(payload, 1U);
    append_f64(payload, 1.0);
    append_f64(payload, 2.0);
    append_f64(payload, 3.0);
    append_u64(payload, 1U);

    std::vector<std::byte> bytes;
    constexpr std::array magic{
        std::byte{'V'}, std::byte{'I'}, std::byte{'C'}, std::byte{'E'},
        std::byte{'W'}, std::byte{'R'}, std::byte{'L'}, std::byte{'D'},
    };
    bytes.insert(bytes.end(), magic.begin(), magic.end());
    append_u32(bytes, 1U);
    append_u32(bytes, 0U);
    append_u32(bytes, 0U);
    append_u32(bytes, 1U);
    append_u32(bytes, 1U);
    append_u32(bytes, 0U);
    append_u64(bytes, fingerprint(payload));
    bytes.insert(bytes.end(), payload.begin(), payload.end());
    return bytes;
}

} // namespace

int main() {
    using namespace vice;

    constexpr std::string_view source = R"(
vice-world-source 1
component 20000000-0000-4000-8000-000000000001 3 "transform state"
resource 30000000-0000-4000-8000-000000000001
region 2 202 1000 0 0 1 0 0 reduced bounds 0 -10 -10 100 10 10
region 1 101 0 0 0 0 0 0 full bounds 0 -10 -10 100 10 10
entity 10000000-0000-4000-8000-000000000002 spatial 1010 2 3 2 transform 0 0 0.70710678 0.70710678 1.5 2 0.75
entity 10000000-0000-4000-8000-000000000001 spatial 5 1 0 1
relation 10000000-0000-4000-8000-000000000001 40000000-0000-4000-8000-000000000001 10000000-0000-4000-8000-000000000002
)";

    const auto parsed = tools::world_cooker::parse_source_manifest(source);
    assert(parsed.snapshot.has_value());
    assert(parsed.diagnostics.empty());
    assert(parsed.snapshot->component_types.size() == 1U);
    assert(parsed.snapshot->resources.size() == 1U);
    assert(parsed.snapshot->regions.size() == 2U);
    assert(parsed.snapshot->entities.size() == 2U);
    assert(parsed.snapshot->relations.size() == 1U);
    assert(parsed.snapshot->regions[0].id == world::RegionId{1});
    assert(parsed.snapshot->regions[1].id == world::RegionId{2});
    assert(parsed.snapshot->regions[1].bounds.has_value());
    assert(!parsed.snapshot->entities[0].authored_transform.has_value());
    assert(parsed.snapshot->entities[1].authored_transform.has_value());
    assert((parsed.snapshot->entities[1].authored_transform->scale == world::AuthoredScale{1.5F, 2.0F, 0.75F}));

    const auto encoded = world::encode_world_snapshot(*parsed.snapshot);
    assert(encoded);
    assert(encoded.diagnostics.empty());

    auto shuffled = *parsed.snapshot;
    std::reverse(shuffled.component_types.begin(), shuffled.component_types.end());
    std::reverse(shuffled.resources.begin(), shuffled.resources.end());
    std::reverse(shuffled.regions.begin(), shuffled.regions.end());
    std::reverse(shuffled.entities.begin(), shuffled.entities.end());
    std::reverse(shuffled.relations.begin(), shuffled.relations.end());
    const auto recooked = world::encode_world_snapshot(shuffled);
    assert(recooked);
    assert(recooked.bytes == encoded.bytes);

    const auto decoded = world::decode_world_snapshot(encoded.bytes);
    assert(decoded.snapshot.has_value());
    assert(decoded.diagnostics.empty());

    world::World restored;
    assert(restored.restore(*decoded.snapshot));
    const auto player_id = world::PersistentId::parse(
        "10000000-0000-4000-8000-000000000001"
    );
    const auto cube_id = world::PersistentId::parse(
        "10000000-0000-4000-8000-000000000002"
    );
    assert(player_id.has_value());
    assert(cube_id.has_value());
    const auto player = restored.find(*player_id);
    const auto cube = restored.find(*cube_id);
    assert(player.has_value());
    assert(cube.has_value());
    assert(restored.spatial_state(*player)->region == world::RegionId{1});
    assert(restored.spatial_state(*cube)->region == world::RegionId{2});
    assert(restored.persistent_id(*player) == player_id);
    assert(restored.persistent_id(*cube) == cube_id);
    assert(!restored.authored_transform(*player).has_value());
    assert(restored.authored_transform(*cube).has_value());
    assert((restored.authored_transform(*cube)->scale == world::AuthoredScale{1.5F, 2.0F, 0.75F}));

    const auto round_trip = world::encode_world_snapshot(restored.snapshot());
    assert(round_trip);
    assert(round_trip.bytes == encoded.bytes);

    const auto legacy = world::decode_world_snapshot(legacy_v1_snapshot());
    assert(legacy.snapshot.has_value());
    assert(legacy.snapshot->entities.size() == 1U);
    assert(legacy.snapshot->entities[0].spatial_state.has_value());
    assert(!legacy.snapshot->entities[0].authored_transform.has_value());
    const auto upgraded_legacy = world::encode_world_snapshot(*legacy.snapshot);
    assert(upgraded_legacy);
    assert(upgraded_legacy.bytes != legacy_v1_snapshot());

    auto corrupted = encoded.bytes;
    corrupted.back() ^= std::byte{0x01};
    const auto rejected = world::decode_world_snapshot(corrupted);
    assert(!rejected.snapshot.has_value());
    assert(!rejected.diagnostics.empty());

    const auto path = std::filesystem::temp_directory_path() / "vice_world_snapshot_test.vworldc";
    std::error_code error;
    std::filesystem::remove(path, error);
    std::string diagnostics;
    assert(world::save_world_snapshot(path, restored.snapshot(), &diagnostics));
    assert(diagnostics.empty());
    const auto loaded = world::load_world_snapshot(path);
    assert(loaded.snapshot.has_value());
    assert(world::encode_world_snapshot(*loaded.snapshot).bytes == encoded.bytes);
    std::filesystem::remove(path, error);

    constexpr std::string_view duplicate_entity = R"(
vice-world-source 1
region 1 1 0 0 0 0 0 0 full none
entity 10000000-0000-4000-8000-000000000001 none
entity 10000000-0000-4000-8000-000000000001 none
)";
    assert(!tools::world_cooker::parse_source_manifest(duplicate_entity).snapshot.has_value());

    constexpr std::string_view invalid_transform = R"(
vice-world-source 1
region 1 1 0 0 0 0 0 0 full none
entity 10000000-0000-4000-8000-000000000001 spatial 0 0 0 1 transform 0 0 0 2 1 1 1
)";
    assert(!tools::world_cooker::parse_source_manifest(invalid_transform).snapshot.has_value());

    return 0;
}
