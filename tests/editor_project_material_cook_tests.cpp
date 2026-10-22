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

    const auto gpu_shader_path = project_root / "assets" / "shaders" / "vice_gpu_pbr_surface.hlsl";
    const auto gpu_material_path = project_root / "assets" / "materials" / "gpu_surface.vmat";
    std::string gpu_shader = R"(
cbuffer SceneFrame : register(b0, space0) {
    column_major float4x4 view_projection;
    float4 frame_marker;
};
StructuredBuffer<float4x4> scene_models : register(t0, space0);
StructuredBuffer<uint> visible_scene_instances : register(t1, space0);
StructuredBuffer<uint4> instance_materials : register(t2, space0);
StructuredBuffer<uint4> material_records : register(t3, space0);
Texture2D<float4> material_textures[] : register(t0, space1);
SamplerState material_samplers[] : register(s0, space1);
Texture2D<float4> lighting_texture_0 : register(t0, space2);
Texture2D<float4> lighting_texture_1 : register(t1, space2);
Texture2D<float4> lighting_texture_2 : register(t2, space2);
Texture2D<float4> lighting_texture_3 : register(t3, space2);
Texture2D<float4> lighting_texture_4 : register(t4, space2);
Texture2D<float4> lighting_texture_5 : register(t5, space2);
Texture2D<float4> lighting_texture_6 : register(t6, space2);
Texture2D<float4> lighting_texture_7 : register(t7, space2);
Texture2D<float4> lighting_texture_8 : register(t8, space2);
Texture2D<float4> lighting_texture_9 : register(t9, space2);
Texture2D<float4> lighting_texture_10 : register(t10, space2);
Texture2D<float4> lighting_texture_11 : register(t11, space2);
Texture2D<float4> lighting_texture_12 : register(t12, space2);
Texture2D<float4> lighting_texture_13 : register(t13, space2);
Texture2D<float4> lighting_texture_14 : register(t14, space2);
Texture2D<float4> lighting_texture_15 : register(t15, space2);
Texture2D<float4> lighting_texture_16 : register(t16, space2);
Texture2D<float4> lighting_texture_17 : register(t17, space2);
Texture2D<float4> lighting_texture_18 : register(t18, space2);
Texture2D<float4> lighting_texture_19 : register(t19, space2);
Texture2D<float4> lighting_texture_20 : register(t20, space2);
Texture2D<float4> lighting_texture_21 : register(t21, space2);
Texture2D<float4> lighting_texture_22 : register(t22, space2);
Texture2D<float4> lighting_texture_23 : register(t23, space2);
Texture2D<float4> lighting_texture_24 : register(t24, space2);
SamplerState lighting_sampler_0 : register(s0, space2);
SamplerState lighting_sampler_1 : register(s1, space2);
struct VertexIn { float3 position : POSITION0; float2 uv : TEXCOORD0; };
struct VertexOut {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    nointerpolation uint scene_instance_index : TEXCOORD1;
};
VertexOut vice_gpu_pbr_vs(VertexIn input, uint instance_id : SV_InstanceID) {
    VertexOut output;
    const uint scene_instance_index = visible_scene_instances[instance_id];
    const float4x4 model = scene_models[scene_instance_index];
    output.position = mul(view_projection, mul(model, float4(input.position, 1.0)));
    output.uv = input.uv;
    output.scene_instance_index = scene_instance_index;
    return output;
}
float4 vice_gpu_pbr_fs(VertexOut input) : SV_Target0 {
    const uint4 instance_material = instance_materials[input.scene_instance_index];
    const uint4 material = material_records[instance_material.x];
    const uint texture_index = NonUniformResourceIndex(material.x);
    const uint sampler_index = NonUniformResourceIndex(material.y);
    float4 value = material_textures[texture_index].Sample(material_samplers[sampler_index], input.uv);
    value += lighting_texture_0.Sample(lighting_sampler_0, input.uv) * 1.0e-7;
    value += lighting_texture_1.Sample(lighting_sampler_0, input.uv) * 1.0e-7;
    value += lighting_texture_2.Sample(lighting_sampler_0, input.uv) * 1.0e-7;
    value += lighting_texture_3.Sample(lighting_sampler_0, input.uv) * 1.0e-7;
    value += lighting_texture_4.Sample(lighting_sampler_0, input.uv) * 1.0e-7;
    value += lighting_texture_5.Sample(lighting_sampler_0, input.uv) * 1.0e-7;
    value += lighting_texture_6.Sample(lighting_sampler_0, input.uv) * 1.0e-7;
    value += lighting_texture_7.Sample(lighting_sampler_0, input.uv) * 1.0e-7;
    value += lighting_texture_8.Sample(lighting_sampler_0, input.uv) * 1.0e-7;
    value += lighting_texture_9.Sample(lighting_sampler_0, input.uv) * 1.0e-7;
    value += lighting_texture_10.Sample(lighting_sampler_0, input.uv) * 1.0e-7;
    value += lighting_texture_11.Sample(lighting_sampler_0, input.uv) * 1.0e-7;
    value += lighting_texture_12.Sample(lighting_sampler_0, input.uv) * 1.0e-7;
    value += lighting_texture_13.Sample(lighting_sampler_0, input.uv) * 1.0e-7;
    value += lighting_texture_14.Sample(lighting_sampler_0, input.uv) * 1.0e-7;
    value += lighting_texture_15.Sample(lighting_sampler_0, input.uv) * 1.0e-7;
    value += lighting_texture_16.Sample(lighting_sampler_1, input.uv) * 1.0e-7;
    value += lighting_texture_17.Sample(lighting_sampler_1, input.uv) * 1.0e-7;
    value += lighting_texture_18.Sample(lighting_sampler_1, input.uv) * 1.0e-7;
    value += lighting_texture_19.Sample(lighting_sampler_1, input.uv) * 1.0e-7;
    value += lighting_texture_20.Sample(lighting_sampler_1, input.uv) * 1.0e-7;
    value += lighting_texture_21.Sample(lighting_sampler_1, input.uv) * 1.0e-7;
    value += lighting_texture_22.Sample(lighting_sampler_1, input.uv) * 1.0e-7;
    value += lighting_texture_23.Sample(lighting_sampler_1, input.uv) * 1.0e-7;
    value += lighting_texture_24.Sample(lighting_sampler_1, input.uv) * 1.0e-7;
    return value + frame_marker.x * 1.0e-7;
}
)";
    write_text(gpu_shader_path, gpu_shader);
    write_text(
        gpu_material_path,
        "vice-render-material 4\n"
        "id 70000000-0000-4000-8000-000000000002\n"
        "shader \"shaders/vice_gpu_pbr_surface.hlsl\"\n"
        "vertex vice_gpu_pbr_vs\n"
        "fragment vice_gpu_pbr_fs\n"
        "base_color 1 1 1 1\n"
        "metallic 0.4\n"
        "roughness 0.3\n"
        "base_color_texture 71000000-0000-4000-8000-000000000001\n"
        "normal_texture 71000000-0000-4000-8000-000000000001\n"
        "orm_texture 71000000-0000-4000-8000-000000000001\n"
        "emissive_texture 71000000-0000-4000-8000-000000000001\n"
        "normal_scale 1\n"
        "emissive_color 0.2 0.3 0.4\n"
        "emissive_intensity 2\n"
        "sampler linear linear linear repeat repeat repeat 4\n"
    );

    const auto gpu_cooked = editor::cook_project_render_materials(*session, texture_ids);
    if (!gpu_cooked.success) std::cerr << gpu_cooked.diagnostics << '\n';
    require(gpu_cooked.success, "GPU PBR v4 project material cooks");
    require(gpu_cooked.materials.size() == 2U, "legacy and GPU PBR materials both cook");
    bool gpu_material_found = false;
    for (const auto& cooked_material : gpu_cooked.materials) {
        const auto candidate = render::load_render_material_asset(cooked_material.cooked_path);
        require(candidate.asset.has_value(), "recooked material loads");
        if (candidate.asset->version == render::render_material_asset_format_version_v4) {
            gpu_material_found = true;
            require(
                render::render_material_uses_gpu_scene_pbr(candidate.asset->version),
                "GPU PBR v4 material keeps the compatibility marker"
            );
        }
    }
    require(gpu_material_found, "GPU PBR v4 cooked material found");

    const std::string old_binding = "register(t24, space2)";
    const auto binding_position = gpu_shader.find(old_binding);
    require(binding_position != std::string::npos, "GPU PBR ABI drift fixture binding found");
    gpu_shader.replace(binding_position, old_binding.size(), "register(t25, space2)");
    write_text(gpu_shader_path, gpu_shader);
    const auto drifted = editor::cook_project_render_materials(*session, texture_ids);
    require(!drifted.success, "GPU PBR v4 rejects shader ABI drift");

    std::filesystem::remove_all(root, error);
    require(!error, "temporary cleanup");
    return 0;
#endif
}