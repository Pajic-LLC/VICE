#include <vice/render/mesh_asset.hpp>
#include <vice/tools/mesh_cooker.hpp>

#include <cassert>
#include <cstddef>
#include <string_view>
#include <vector>

int main() {
    using namespace vice;

    constexpr std::string_view source = R"(
vice-mesh-source 1
name test_ramp
id 50000000-0000-4000-8000-000000000099
collision triangle_mesh
vertex 0 0 0 0.2 0.4 0.8
vertex 0 0 1 0.2 0.5 0.9
vertex 2 1 0 0.8 0.4 0.2
vertex 2 1 1 0.9 0.5 0.2
triangle 0 2 1
triangle 1 2 3
)";

    const auto parsed = tools::mesh_cooker::parse_source_manifest(source);
    assert(parsed.source.has_value());
    assert(parsed.diagnostics.empty());
    assert(parsed.source->name == "test_ramp");
    assert(render::valid(parsed.source->stable_id));
    assert(parsed.source->collision == tools::mesh_cooker::CollisionMode::triangle_mesh);
    assert(parsed.source->vertices.size() == 4U);
    assert(parsed.source->indices.size() == 6U);

    const auto cooked = tools::mesh_cooker::cook(*parsed.source);
    assert(cooked.success);
    assert(cooked.diagnostics.empty());
    assert(cooked.source_fingerprint != 0U);
    assert(!cooked.bytes.empty());

    const auto recooked = tools::mesh_cooker::cook(*parsed.source);
    assert(recooked.success);
    assert(recooked.source_fingerprint == cooked.source_fingerprint);
    assert(recooked.bytes == cooked.bytes);

    const auto decoded = render::decode_mesh_asset(cooked.bytes);
    assert(decoded.asset.has_value());
    assert(decoded.diagnostics.empty());
    assert(decoded.asset->version == render::mesh_asset_format_version);
    assert(decoded.asset->stable_id == parsed.source->stable_id);
    assert(decoded.asset->source_fingerprint == cooked.source_fingerprint);
    assert(decoded.asset->vertices.size() == 4U);
    assert(decoded.asset->indices.size() == 6U);
    assert(decoded.asset->indices[5] == 3U);

    auto legacy = cooked.bytes;
    legacy[8] = std::byte{1};
    legacy[9] = std::byte{0};
    legacy[10] = std::byte{0};
    legacy[11] = std::byte{0};
    legacy.erase(legacy.begin() + 28, legacy.begin() + 44);
    const auto legacy_decoded = render::decode_mesh_asset(legacy);
    assert(legacy_decoded.asset.has_value());
    assert(legacy_decoded.asset->version == render::mesh_asset_format_version_v1);
    assert(!render::valid(legacy_decoded.asset->stable_id));
    assert(legacy_decoded.asset->vertices.size() == decoded.asset->vertices.size());
    assert(legacy_decoded.asset->vertices[2].position[0] == decoded.asset->vertices[2].position[0]);
    assert(legacy_decoded.asset->indices == decoded.asset->indices);

    auto corrupted = cooked.bytes;
    corrupted.back() ^= std::byte{0x01};
    const auto corrupt_decode = render::decode_mesh_asset(corrupted);
    assert(!corrupt_decode.asset.has_value());
    assert(!corrupt_decode.diagnostics.empty());

    constexpr std::string_view no_id = R"(
vice-mesh-source 1
name legacy_source
vertex 0 0 0 1 1 1
vertex 1 0 0 1 1 1
vertex 0 1 0 1 1 1
triangle 0 1 2
)";
    const auto legacy_source = tools::mesh_cooker::parse_source_manifest(no_id);
    assert(legacy_source.source.has_value());
    assert(!render::valid(legacy_source.source->stable_id));
    assert(tools::mesh_cooker::cook(*legacy_source.source).success);

    constexpr std::string_view invalid_id = R"(
vice-mesh-source 1
name invalid_id
id definitely-not-a-uuid
vertex 0 0 0 1 1 1
vertex 1 0 0 1 1 1
vertex 0 1 0 1 1 1
triangle 0 1 2
)";
    assert(!tools::mesh_cooker::parse_source_manifest(invalid_id).source.has_value());

    constexpr std::string_view invalid_index = R"(
vice-mesh-source 1
name invalid
vertex 0 0 0 1 1 1
vertex 1 0 0 1 1 1
vertex 0 1 0 1 1 1
triangle 0 1 9
)";
    assert(!tools::mesh_cooker::parse_source_manifest(invalid_index).source.has_value());

    constexpr std::string_view invalid_color = R"(
vice-mesh-source 1
name invalid_color
vertex 0 0 0 2 1 1
vertex 1 0 0 1 1 1
vertex 0 1 0 1 1 1
triangle 0 1 2
)";
    assert(!tools::mesh_cooker::parse_source_manifest(invalid_color).source.has_value());

    return 0;
}
