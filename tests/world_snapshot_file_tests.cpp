#include <vice/tools/world_cooker.hpp>
#include <vice/world/snapshot_file.hpp>
#include <vice/world/world.hpp>

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <filesystem>
#include <string_view>

int main() {
    using namespace vice;

    constexpr std::string_view source = R"(
vice-world-source 1
component 20000000-0000-4000-8000-000000000001 3 "transform state"
resource 30000000-0000-4000-8000-000000000001
region 2 202 1000 0 0 1 0 0 reduced bounds 0 -10 -10 100 10 10
region 1 101 0 0 0 0 0 0 full bounds 0 -10 -10 100 10 10
entity 10000000-0000-4000-8000-000000000002 spatial 1010 2 3 2
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

    const auto round_trip = world::encode_world_snapshot(restored.snapshot());
    assert(round_trip);
    assert(round_trip.bytes == encoded.bytes);

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

    return 0;
}
