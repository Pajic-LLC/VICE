#include <vice/render/material_asset.hpp>
#include <vice/tools/render_material_cooker.hpp>

#include <cassert>
#include <cstddef>
#include <string_view>

int main() {
    using namespace vice;

    constexpr std::string_view source = R"(
vice-render-material 1
id 70000000-0000-4000-8000-000000000001
shader "shaders/surface.hlsl"
vertex surface_vs
fragment surface_fs
base_color 0.25 0.5 0.75 1
)";

    const auto parsed = tools::render_material_cooker::parse_source_manifest(source);
    assert(parsed.source.has_value());
    assert(parsed.diagnostics.empty());
    assert(parsed.source->shader_source.generic_string() == "shaders/surface.hlsl");
    assert(parsed.source->vertex_entry == "surface_vs");
    assert(parsed.source->fragment_entry == "surface_fs");
    assert(parsed.source->base_color.red == 0.25F);
    assert(parsed.source->base_color.green == 0.5F);
    assert(parsed.source->base_color.blue == 0.75F);
    assert(parsed.source->base_color.alpha == 1.0F);
    assert(parsed.source->source_fingerprint != 0U);

    const auto cooked = tools::render_material_cooker::cook(
        *parsed.source,
        "surface.shaders/vertex/shader.vice.json",
        "surface.shaders/fragment/shader.vice.json"
    );
    assert(cooked.success);
    assert(cooked.diagnostics.empty());
    assert(!cooked.bytes.empty());

    const auto cooked_again = tools::render_material_cooker::cook(
        *parsed.source,
        "surface.shaders/vertex/shader.vice.json",
        "surface.shaders/fragment/shader.vice.json"
    );
    assert(cooked_again.success);
    assert(cooked_again.bytes == cooked.bytes);

    const auto decoded = render::decode_render_material_asset(cooked.bytes);
    assert(decoded.asset.has_value());
    assert(decoded.diagnostics.empty());
    assert(decoded.asset->stable_id == parsed.source->stable_id);
    assert(decoded.asset->source_fingerprint == parsed.source->source_fingerprint);
    assert(decoded.asset->base_color.red == 0.25F);
    assert(decoded.asset->base_color.green == 0.5F);
    assert(decoded.asset->base_color.blue == 0.75F);
    assert(decoded.asset->base_color.alpha == 1.0F);
    assert(decoded.asset->vertex_shader_manifest == "surface.shaders/vertex/shader.vice.json");
    assert(decoded.asset->fragment_shader_manifest == "surface.shaders/fragment/shader.vice.json");

    auto corrupted = cooked.bytes;
    corrupted.back() ^= std::byte{0x01};
    assert(!render::decode_render_material_asset(corrupted).asset.has_value());

    constexpr std::string_view traversal = R"(
vice-render-material 1
id 70000000-0000-4000-8000-000000000001
shader "../surface.hlsl"
vertex surface_vs
fragment surface_fs
base_color 1 1 1 1
)";
    assert(!tools::render_material_cooker::parse_source_manifest(traversal).source.has_value());

    constexpr std::string_view invalid_entry = R"(
vice-render-material 1
id 70000000-0000-4000-8000-000000000001
shader "shaders/surface.hlsl"
vertex 7surface
fragment surface_fs
base_color 1 1 1 1
)";
    assert(!tools::render_material_cooker::parse_source_manifest(invalid_entry).source.has_value());

    constexpr std::string_view invalid_color = R"(
vice-render-material 1
id 70000000-0000-4000-8000-000000000001
shader "shaders/surface.hlsl"
vertex surface_vs
fragment surface_fs
base_color -1 1 1 1
)";
    assert(!tools::render_material_cooker::parse_source_manifest(invalid_color).source.has_value());

    render::RenderMaterialAsset unsafe = *decoded.asset;
    unsafe.vertex_shader_manifest = "../shader.vice.json";
    assert(!render::encode_render_material_asset(unsafe));

    return 0;
}
