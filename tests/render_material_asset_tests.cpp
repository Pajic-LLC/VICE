#include <vice/render/material_asset.hpp>
#include <vice/tools/render_material_cooker.hpp>

#include <cassert>
#include <cmath>
#include <string>

namespace {

bool nearly_equal(const float left, const float right) {
    return std::abs(left - right) < 0.0001F;
}

} // namespace

int main() {
    using namespace vice;

    const std::string source_v1 =
        "vice-render-material 1\n"
        "id 70000000-0000-4000-8000-000000000001\n"
        "shader \"shaders/surface.hlsl\"\n"
        "vertex surface_vs\n"
        "fragment surface_fs\n"
        "base_color 0.2 0.4 0.8 1\n";

    const auto parsed_v1 = tools::render_material_cooker::parse_source_manifest(source_v1);
    assert(parsed_v1.source.has_value());
    assert(parsed_v1.source->version == tools::render_material_cooker::render_material_source_format_version_v1);

    const auto cooked_v1 = tools::render_material_cooker::cook(
        *parsed_v1.source,
        "surface.shaders/vertex/shader.vice.json",
        "surface.shaders/fragment/shader.vice.json"
    );
    assert(cooked_v1.success);
    const auto decoded_v1 = render::decode_render_material_asset(cooked_v1.bytes);
    assert(decoded_v1.asset.has_value());
    assert(decoded_v1.asset->version == render::render_material_asset_format_version_v1);
    assert(nearly_equal(decoded_v1.asset->base_color.blue, 0.8F));
    assert(!render::valid_texture_id(decoded_v1.asset->base_color_texture));

    const std::string source_v2 =
        "vice-render-material 2\n"
        "id 70000000-0000-4000-8000-000000000002\n"
        "shader \"shaders/pbr.hlsl\"\n"
        "vertex pbr_vs\n"
        "fragment pbr_fs\n"
        "base_color 0.9 0.8 0.7 1\n"
        "metallic 0.35\n"
        "roughness 0.42\n"
        "base_color_texture 71000000-0000-4000-8000-000000000001\n"
        "sampler linear linear linear repeat mirror clamp 8\n";

    const auto parsed_v2 = tools::render_material_cooker::parse_source_manifest(source_v2);
    assert(parsed_v2.source.has_value());
    assert(parsed_v2.source->version == tools::render_material_cooker::render_material_source_format_version_v2);
    assert(nearly_equal(parsed_v2.source->metallic, 0.35F));
    assert(nearly_equal(parsed_v2.source->roughness, 0.42F));
    assert(parsed_v2.source->max_anisotropy == 8U);

    const auto cooked_v2 = tools::render_material_cooker::cook(
        *parsed_v2.source,
        "pbr.shaders/vertex/shader.vice.json",
        "pbr.shaders/fragment/shader.vice.json"
    );
    assert(cooked_v2.success);
    const auto decoded_v2 = render::decode_render_material_asset(cooked_v2.bytes);
    assert(decoded_v2.asset.has_value());
    assert(decoded_v2.asset->version == render::render_material_asset_format_version_v2);
    assert(nearly_equal(decoded_v2.asset->metallic, 0.35F));
    assert(nearly_equal(decoded_v2.asset->roughness, 0.42F));
    assert(render::valid_texture_id(decoded_v2.asset->base_color_texture));
    assert(decoded_v2.asset->min_filter == render::Filter::linear);
    assert(decoded_v2.asset->address_u == render::AddressMode::repeat);
    assert(decoded_v2.asset->address_v == render::AddressMode::mirrored_repeat);
    assert(decoded_v2.asset->address_w == render::AddressMode::clamp_to_edge);
    assert(decoded_v2.asset->max_anisotropy == 8U);

    const std::string source_v3 =
        "vice-render-material 3\n"
        "id 70000000-0000-4000-8000-000000000004\n"
        "shader \"shaders/pbr_v3.hlsl\"\n"
        "vertex pbr_vs\n"
        "fragment pbr_fs\n"
        "base_color 0.8 0.7 0.6 1\n"
        "metallic 0.2\n"
        "roughness 0.45\n"
        "base_color_texture 71000000-0000-4000-8000-000000000001\n"
        "normal_texture 71000000-0000-4000-8000-000000000002\n"
        "orm_texture 71000000-0000-4000-8000-000000000003\n"
        "emissive_texture 71000000-0000-4000-8000-000000000004\n"
        "normal_scale 0.75\n"
        "emissive_color 1.5 0.4 0.2\n"
        "emissive_intensity 3.0\n"
        "sampler linear linear linear repeat repeat repeat 8\n";

    const auto parsed_v3 = tools::render_material_cooker::parse_source_manifest(source_v3);
    assert(parsed_v3.source.has_value());
    assert(parsed_v3.source->version == tools::render_material_cooker::render_material_source_format_version_v3);
    const auto cooked_v3 = tools::render_material_cooker::cook(
        *parsed_v3.source,
        "pbr_v3.shaders/vertex/shader.vice.json",
        "pbr_v3.shaders/fragment/shader.vice.json"
    );
    assert(cooked_v3.success);
    const auto decoded_v3 = render::decode_render_material_asset(cooked_v3.bytes);
    assert(decoded_v3.asset.has_value());
    assert(decoded_v3.asset->version == render::render_material_asset_format_version_v3);
    assert(render::valid_texture_id(decoded_v3.asset->normal_texture));
    assert(render::valid_texture_id(decoded_v3.asset->orm_texture));
    assert(render::valid_texture_id(decoded_v3.asset->emissive_texture));
    assert(nearly_equal(decoded_v3.asset->normal_scale, 0.75F));
    assert(nearly_equal(decoded_v3.asset->emissive_color.red, 1.5F));
    assert(nearly_equal(decoded_v3.asset->emissive_intensity, 3.0F));

    const std::string incomplete_v3 =
        "vice-render-material 3\n"
        "id 70000000-0000-4000-8000-000000000005\n"
        "shader \"shaders/pbr.hlsl\"\n"
        "vertex pbr_vs\n"
        "fragment pbr_fs\n"
        "base_color 1 1 1 1\n"
        "metallic 0\n"
        "roughness 1\n"
        "base_color_texture 71000000-0000-4000-8000-000000000001\n"
        "normal_texture 71000000-0000-4000-8000-000000000002\n"
        "orm_texture 71000000-0000-4000-8000-000000000003\n"
        "normal_scale 1\n"
        "emissive_color 1 1 1\n"
        "emissive_intensity 1\n"
        "sampler linear linear linear repeat repeat repeat 1\n";
    assert(!tools::render_material_cooker::parse_source_manifest(incomplete_v3).source.has_value());

    const auto cooked_v2_again = tools::render_material_cooker::cook(
        *parsed_v2.source,
        "pbr.shaders/vertex/shader.vice.json",
        "pbr.shaders/fragment/shader.vice.json"
    );
    assert(cooked_v2_again.success);
    assert(cooked_v2_again.bytes == cooked_v2.bytes);

    auto corrupted = cooked_v2.bytes;
    corrupted.back() ^= std::byte{0x01U};
    assert(!render::decode_render_material_asset(corrupted).asset.has_value());

    const std::string missing_texture =
        "vice-render-material 2\n"
        "id 70000000-0000-4000-8000-000000000003\n"
        "shader \"shaders/pbr.hlsl\"\n"
        "vertex pbr_vs\n"
        "fragment pbr_fs\n"
        "base_color 1 1 1 1\n"
        "metallic 0\n"
        "roughness 1\n"
        "sampler linear linear linear repeat repeat repeat 1\n";
    assert(!tools::render_material_cooker::parse_source_manifest(missing_texture).source.has_value());

    const std::string bad_sampler =
        "vice-render-material 2\n"
        "id 70000000-0000-4000-8000-000000000003\n"
        "shader \"shaders/pbr.hlsl\"\n"
        "vertex pbr_vs\n"
        "fragment pbr_fs\n"
        "base_color 1 1 1 1\n"
        "metallic 0\n"
        "roughness 1\n"
        "base_color_texture 71000000-0000-4000-8000-000000000001\n"
        "sampler nearest nearest nearest repeat repeat repeat 4\n";
    assert(!tools::render_material_cooker::parse_source_manifest(bad_sampler).source.has_value());

    render::RenderMaterialAsset unsafe = *decoded_v2.asset;
    unsafe.vertex_shader_manifest = "../escape/shader.vice.json";
    assert(!render::encode_render_material_asset(unsafe));

    return 0;
}
