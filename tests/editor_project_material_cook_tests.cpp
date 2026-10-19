#include <vice/editor/project_material_cook.hpp>
#include <vice/editor/project_session.hpp>
#include <vice/editor/project_texture_cook.hpp>
#include <vice/render/material_asset.hpp>
#include <vice/render/texture_asset.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

namespace {

void require(const bool condition, const std::string_view message) {
    if (condition) return;
    std::cerr << "editor project material cook test failed: " << message << '\n';
    std::exit(1);
}

void write_text(const std::filesystem::path& path, const std::string_view text) {
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    require(!error, "asset directory creation");
    std::ofstream output{path, std::ios::binary | std::ios::trunc};
    require(static_cast<bool>(output), "asset file open");
    output << text;
    require(static_cast<bool>(output), "asset file write");
}

} // namespace

int main() {
#ifndef _WIN32
    return 0;
#else
    using namespace vice;

    std::error_code error;
    const auto root = std::filesystem::temp_directory_path(error) /
        "vice_editor_project_material_cook_test_71000000";
    require(!error, "temporary directory path");
    std::filesystem::remove_all(root, error);
    error.clear();

    const auto project_root = root / "project";
    const auto manifest_path = project_root / "vice.viceproj";
    const auto world_path = project_root / "assets" / "worlds" / "main.vworld";
    const auto shader_path = project_root / "assets" / "shaders" / "surface.hlsl";
    const auto material_path = project_root / "assets" / "materials" / "surface.vmat";
    const auto texture_path = project_root / "assets" / "textures" / "surface.vtex";
    const auto image_path = project_root / "assets" / "textures" / "surface.ppm";

    write_text(
        manifest_path,
        "vice-project 1\n"
        "id 71000000-0000-4000-8000-000000000011\n"
        "name \"Material Cook Test\"\n"
        "game_name \"Material Cook Test\"\n"
        "game_target \"material-cook-test\"\n"
        "assets \"assets\"\n"
        "startup_world \"worlds/main.vworld\"\n"
    );
    write_text(
        world_path,
        "vice-world-source 1\n"
        "region 1 1 0 0 0 0 0 0 full bounds -10 -10 -10 10 10 10\n"
    );
    write_text(
        image_path,
        "P3\n2 1\n255\n255 64 32  32 128 255\n"
    );
    write_text(
        texture_path,
        "vice-texture-source 1\n"
        "id 71000000-0000-4000-8000-000000000001\n"
        "image \"textures/surface.ppm\"\n"
        "color_space srgb\n"
    );
    write_text(
        shader_path,
        R"(
cbuffer SceneFrame : register(b0, space0) {
    column_major float4x4 view_projection;
    column_major float4x4 model;
    float4 camera_position;
    float4 light_direction;
    float4 light_color;
};
cbuffer MaterialParams : register(b1, space0) {
    float4 base_color;
    float metallic;
    float roughness;
    float2 padding;
};
Texture2D<float4> base_color_texture : register(t0, space0);
Texture2D<float> shadow_1 : register(t1, space0);
Texture2D<float> shadow_2 : register(t2, space0);
Texture2D<float> shadow_3 : register(t3, space0);
Texture2D<float> shadow_4 : register(t4, space0);
Texture2D<float> point_shadow_0 : register(t5, space0);
Texture2D<float> point_shadow_1 : register(t6, space0);
Texture2D<float> point_shadow_2 : register(t7, space0);
Texture2D<float> point_shadow_3 : register(t8, space0);
Texture2D<float> point_shadow_4 : register(t9, space0);
Texture2D<float> point_shadow_5 : register(t10, space0);
Texture2D<float> point_shadow_6 : register(t11, space0);
Texture2D<float> point_shadow_7 : register(t12, space0);
Texture2D<float> point_shadow_8 : register(t13, space0);
Texture2D<float> point_shadow_9 : register(t14, space0);
Texture2D<float> point_shadow_10 : register(t15, space0);
Texture2D<float> point_shadow_11 : register(t16, space0);
SamplerState base_color_sampler : register(s0, space0);
SamplerState shadow_sampler : register(s1, space0);
struct VertexIn {
    float3 position : POSITION0;
    float3 color : COLOR0;
    float3 normal : NORMAL0;
    float2 uv : TEXCOORD0;
};
struct VertexOut {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};
VertexOut surface_vs(VertexIn input) {
    VertexOut output;
    output.position = mul(view_projection, mul(model, float4(input.position, 1.0)));
    output.uv = input.uv;
    return output;
}
float4 surface_fs(VertexOut input) : SV_Target0 {
    const float4 sampled = base_color_texture.Sample(base_color_sampler, input.uv);
    const float keep_shadow_resources =
        shadow_1.Sample(shadow_sampler, input.uv) +
        shadow_2.Sample(shadow_sampler, input.uv) +
        shadow_3.Sample(shadow_sampler, input.uv) +
        shadow_4.Sample(shadow_sampler, input.uv) +
        point_shadow_0.Sample(shadow_sampler, input.uv) +
        point_shadow_1.Sample(shadow_sampler, input.uv) +
        point_shadow_2.Sample(shadow_sampler, input.uv) +
        point_shadow_3.Sample(shadow_sampler, input.uv) +
        point_shadow_4.Sample(shadow_sampler, input.uv) +
        point_shadow_5.Sample(shadow_sampler, input.uv) +
        point_shadow_6.Sample(shadow_sampler, input.uv) +
        point_shadow_7.Sample(shadow_sampler, input.uv) +
        point_shadow_8.Sample(shadow_sampler, input.uv) +
        point_shadow_9.Sample(shadow_sampler, input.uv) +
        point_shadow_10.Sample(shadow_sampler, input.uv) +
        point_shadow_11.Sample(shadow_sampler, input.uv);
    return sampled * base_color *
        float4(1.0 - metallic * 0.1, roughness, 1.0, 1.0) *
        (1.0 + keep_shadow_resources * 1.0e-7);
}
)"
    );
    write_text(
        material_path,
        "vice-render-material 2\n"
        "id 70000000-0000-4000-8000-000000000001\n"
        "shader \"shaders/surface.hlsl\"\n"
        "vertex surface_vs\n"
        "fragment surface_fs\n"
        "base_color 0.3 0.6 0.9 1\n"
        "metallic 0.25\n"
        "roughness 0.55\n"
        "base_color_texture 71000000-0000-4000-8000-000000000001\n"
        "sampler linear linear linear repeat repeat repeat 4\n"
    );

    std::string diagnostics;
    const auto session = editor::ProjectSession::open(
        manifest_path,
        root / "cache",
        root / "state",
        &diagnostics
    );
    require(session.has_value(), "project session opens");
    require(diagnostics.empty(), "project session diagnostics");

    const auto textures = editor::cook_project_textures(*session);
    if (!textures.success) std::cerr << textures.diagnostics << '\n';
    require(textures.success, "project texture cooks");
    require(textures.textures.size() == 1U, "one texture cooked");
    require(std::filesystem::is_regular_file(textures.textures.front().cooked), "cooked texture exists");
    const auto loaded_texture = render::load_texture_asset(textures.textures.front().cooked);
    require(loaded_texture.asset.has_value(), "cooked texture loads");
    require(loaded_texture.asset->width == 2U, "texture width preserved");
    require(loaded_texture.asset->color_space == render::TextureColorSpace::srgb, "texture is sRGB");

    const auto texture_ids = editor::cooked_texture_ids(textures);
    const auto cooked = editor::cook_project_render_materials(*session, texture_ids);
    if (!cooked.success) std::cerr << cooked.diagnostics << '\n';
    require(cooked.success, "project render material cooks");
    require(cooked.materials.size() == 1U, "one render material cooked");

    const auto& material = cooked.materials.front();
    require(std::filesystem::is_regular_file(material.cooked_path), "cooked material exists");
    require(std::filesystem::is_regular_file(material.vertex_manifest), "cooked vertex manifest exists");
    require(std::filesystem::is_regular_file(material.fragment_manifest), "cooked fragment manifest exists");

    const auto loaded = render::load_render_material_asset(material.cooked_path);
    require(loaded.asset.has_value(), "cooked material loads");
    require(loaded.asset->version == render::render_material_asset_format_version_v2, "material v2 preserved");
    require(render::valid_texture_id(loaded.asset->base_color_texture), "material texture identity preserved");
    require(
        loaded.asset->vertex_shader_manifest == "surface.shaders/vertex/shader.vice.json",
        "vertex manifest is staged-relative"
    );
    require(
        loaded.asset->fragment_shader_manifest == "surface.shaders/fragment/shader.vice.json",
        "fragment manifest is staged-relative"
    );

    const auto missing_texture = editor::cook_project_render_materials(*session, {});
    require(!missing_texture.success, "material rejects an uncooked texture dependency");

    std::filesystem::remove_all(root, error);
    require(!error, "temporary cleanup");
    return 0;
#endif
}
