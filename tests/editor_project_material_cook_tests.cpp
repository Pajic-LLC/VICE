#include <vice/editor/project_material_cook.hpp>
#include <vice/editor/project_session.hpp>
#include <vice/render/material_asset.hpp>

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

    write_text(
        manifest_path,
        "vice-project 1\n"
        "id 71000000-0000-4000-8000-000000000001\n"
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
        shader_path,
        R"(
cbuffer SceneFrame : register(b0, space0) {
    column_major float4x4 view_projection;
    column_major float4x4 model;
};
cbuffer MaterialParams : register(b1, space0) {
    float4 base_color;
};
struct VertexIn {
    float3 position : POSITION0;
    float3 color : COLOR0;
};
struct VertexOut {
    float4 position : SV_Position;
    float3 color : COLOR0;
};
VertexOut surface_vs(VertexIn input) {
    VertexOut output;
    output.position = mul(view_projection, mul(model, float4(input.position, 1.0)));
    output.color = input.color;
    return output;
}
float4 surface_fs(VertexOut input) : SV_Target0 {
    return float4(input.color, 1.0) * base_color;
}
)"
    );
    write_text(
        material_path,
        "vice-render-material 1\n"
        "id 70000000-0000-4000-8000-000000000001\n"
        "shader \"shaders/surface.hlsl\"\n"
        "vertex surface_vs\n"
        "fragment surface_fs\n"
        "base_color 0.3 0.6 0.9 1\n"
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

    const auto cooked = editor::cook_project_render_materials(*session);
    if (!cooked.success) {
        std::cerr << cooked.diagnostics << '\n';
    }
    require(cooked.success, "project render material cooks");
    require(cooked.materials.size() == 1U, "one render material cooked");

    const auto& material = cooked.materials.front();
    require(std::filesystem::is_regular_file(material.cooked_path), "cooked material exists");
    require(std::filesystem::is_regular_file(material.vertex_manifest), "cooked vertex manifest exists");
    require(std::filesystem::is_regular_file(material.fragment_manifest), "cooked fragment manifest exists");

    const auto loaded = render::load_render_material_asset(material.cooked_path);
    require(loaded.asset.has_value(), "cooked material loads");
    require(
        loaded.asset->vertex_shader_manifest == "surface.shaders/vertex/shader.vice.json",
        "vertex manifest is staged-relative"
    );
    require(
        loaded.asset->fragment_shader_manifest == "surface.shaders/fragment/shader.vice.json",
        "fragment manifest is staged-relative"
    );

    std::filesystem::remove_all(root, error);
    require(!error, "temporary cleanup");
    return 0;
#endif
}
