#include <vice/tools/world_cooker.hpp>
#include <vice/world/snapshot_file.hpp>
#include <vice/world/world.hpp>

#include <cassert>
#include <string_view>

int main() {
    using namespace vice;

    constexpr std::string_view source = R"(
vice-world-source 1
resource 50000000-0000-4000-8000-000000000001
resource 70000000-0000-4000-8000-000000000001
region 1 1 0 0 0 0 0 0 full bounds -10 -10 -10 10 10 10
entity 10000000-0000-4000-8000-000000000001 spatial 0 0 0 1 resource render_mesh 50000000-0000-4000-8000-000000000001 resource render_material 70000000-0000-4000-8000-000000000001
)";

    const auto parsed = tools::world_cooker::parse_source_manifest(source);
    assert(parsed.snapshot.has_value());
    assert(parsed.diagnostics.empty());
    assert(parsed.snapshot->entities.size() == 1U);
    assert(parsed.snapshot->entities.front().resource_references.size() == 2U);

    const auto encoded = world::encode_world_snapshot(*parsed.snapshot);
    assert(encoded);
    const auto decoded = world::decode_world_snapshot(encoded.bytes);
    assert(decoded.snapshot.has_value());
    assert(decoded.diagnostics.empty());

    world::World restored;
    assert(restored.restore(*decoded.snapshot));
    const auto entity_id = world::PersistentId::parse("10000000-0000-4000-8000-000000000001");
    const auto mesh_id = world::PersistentId::parse("50000000-0000-4000-8000-000000000001");
    const auto material_id = world::PersistentId::parse("70000000-0000-4000-8000-000000000001");
    assert(entity_id.has_value());
    assert(mesh_id.has_value());
    assert(material_id.has_value());

    const auto entity = restored.find(*entity_id);
    assert(entity.has_value());
    assert(
        restored.resource_reference(*entity, world::ResourceRole::render_mesh) ==
        std::optional{world::ResourceId{*mesh_id}}
    );
    assert(
        restored.resource_reference(*entity, world::ResourceRole::render_material) ==
        std::optional{world::ResourceId{*material_id}}
    );

    const auto round_trip = world::encode_world_snapshot(restored.snapshot());
    assert(round_trip);
    assert(round_trip.bytes == encoded.bytes);
    return 0;
}
