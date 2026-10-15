#include <vice/tools/shader_cooker.hpp>

#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>

int main() {
    using namespace vice::tools::shader_cooker;

    assert(shader_stage_name(ShaderStage::vertex) == "vertex");
    assert(shader_stage_name(ShaderStage::fragment) == "fragment");
    assert(shader_stage_name(ShaderStage::compute) == "compute");

    assert(shader_register_class_name(ShaderRegisterClass::constant_buffer) == "constant_buffer");
    assert(shader_register_class_name(ShaderRegisterClass::shader_resource) == "shader_resource");
    assert(shader_register_class_name(ShaderRegisterClass::unordered_access) == "unordered_access");
    assert(shader_register_class_name(ShaderRegisterClass::sampler) == "sampler");

    assert(shader_profile(ShaderStage::vertex, "6.0") == "vs_6_0");
    assert(shader_profile(ShaderStage::fragment, "6.6") == "ps_6_6");
    assert(shader_profile(ShaderStage::compute, "6.9") == "cs_6_9");

    constexpr const char* assembly = R"DXC(
;
; Resource Bindings:
;
; Name                                 Type  Format         Dim      ID      HLSL Bind  Count
; ------------------------------ ---------- ------- ----------- ------- -------------- ------
; FrameData                         cbuffer      NA          NA     CB0     cb2,space1      1
; LinearSampler                     sampler      NA          NA      S0      s3,space1      1
; InputTexture                      texture     f32          2d      T0      t4,space2      6
; OutputBuffer                          UAV  struct         r/w      U0      u7,space3 unbounded
;
; ViewId state:
)DXC";

    const ShaderReflection reflection = parse_dxc_resource_bindings(assembly);
    assert(reflection.valid);
    assert(reflection.diagnostics.empty());
    assert(reflection.resources.size() == 4U);

    const auto& constant_buffer = reflection.resources[0];
    assert(constant_buffer.name == "FrameData");
    assert(constant_buffer.register_class == ShaderRegisterClass::constant_buffer);
    assert(constant_buffer.binding == 2U);
    assert(constant_buffer.space == 1U);
    assert(constant_buffer.count == 1U);
    assert(!constant_buffer.unbounded);

    const auto& sampler = reflection.resources[1];
    assert(sampler.register_class == ShaderRegisterClass::sampler);
    assert(sampler.binding == 3U);
    assert(sampler.space == 1U);

    const auto& texture = reflection.resources[2];
    assert(texture.register_class == ShaderRegisterClass::shader_resource);
    assert(texture.binding == 4U);
    assert(texture.space == 2U);
    assert(texture.count == 6U);
    assert(texture.type == "texture");
    assert(texture.format == "f32");
    assert(texture.dimension == "2d");

    const auto& unordered_access = reflection.resources[3];
    assert(unordered_access.register_class == ShaderRegisterClass::unordered_access);
    assert(unordered_access.binding == 7U);
    assert(unordered_access.space == 3U);
    assert(unordered_access.count == 0U);
    assert(unordered_access.unbounded);

    const ShaderReflection empty_reflection = parse_dxc_resource_bindings(R"DXC(
; Resource Bindings:
;
; Name                                 Type  Format         Dim      ID      HLSL Bind  Count
; ------------------------------ ---------- ------- ----------- ------- -------------- ------
;
)DXC");
    assert(empty_reflection.valid);
    assert(empty_reflection.resources.empty());

    const ShaderReflection malformed_reflection = parse_dxc_resource_bindings("; no bindings here\n");
    assert(!malformed_reflection.valid);
    assert(!malformed_reflection.diagnostics.empty());

    CookRequest request;
    request.entry_point = "main_vs";
    request.stage = ShaderStage::vertex;
    request.target = CookTarget::all;
    request.shader_model = "6.0";

    const std::string source =
        "float4 main_vs(uint vertex_id : SV_VertexID) : SV_Position { "
        "return float4(vertex_id, 0, 0, 1); }";

    const auto first = make_cache_key(source, request, "dxc 1", "msc 1");
    const auto second = make_cache_key(source, request, "dxc 1", "msc 1");
    assert(first == second);
    assert(first.size() == 64U);

    request.entry_point = "other_vs";
    const auto changed_entry = make_cache_key(source, request, "dxc 1", "msc 1");
    assert(first != changed_entry);

    request.entry_point = "main_vs";
    request.debug = true;
    const auto debug_key = make_cache_key(source, request, "dxc 1", "msc 1");
    assert(first != debug_key);

    const auto temporary =
        std::filesystem::temp_directory_path() / "vice_shader_cooker_discovery_test";
#ifdef _WIN32
    const auto fake_tool = temporary / "vice-fake-tool.exe";
#else
    const auto fake_tool = temporary / "vice-fake-tool";
#endif
    std::filesystem::create_directories(temporary);
    {
        std::ofstream stream(fake_tool);
        stream << "fake";
    }
    assert(discover_executable(fake_tool, "VICE_TEST_UNUSED", "vice-fake-tool") == fake_tool);
    std::filesystem::remove_all(temporary);

    return 0;
}
