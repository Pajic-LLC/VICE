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
    assert(decoded.asset->source_fingerprint == cooked.source_fingerprint);
    assert(decoded.asset->vertices.size() == 4U);
    assert(decoded.asset->indices.size() == 6U);
    assert(decoded.asset->indices[5] == 3U);

    auto corrupted = cooked.bytes;
    corrupted.back() ^= std::byte{0x01};
    const auto corrupt_decode = render::decode_mesh_asset(corrupted);
    assert(!corrupt_decode.asset.has_value());
    assert(!corrupt_decode.diagnostics.empty());

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
