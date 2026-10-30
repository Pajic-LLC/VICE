#include <vice/tools/metal_reflection.hpp>
#include <vice/tools/shader_cooker.hpp>

#include <filesystem>
#include <fstream>
#include <string>

#define CHECK(condition) do { if (!(condition)) return __LINE__; } while (false)

int main() {
    using namespace vice::tools::shader_cooker;

    CHECK(shader_stage_name(ShaderStage::vertex) == "vertex");
    CHECK(shader_stage_name(ShaderStage::fragment) == "fragment");
    CHECK(shader_stage_name(ShaderStage::compute) == "compute");

    CHECK(shader_register_class_name(ShaderRegisterClass::constant_buffer) == "constant_buffer");
    CHECK(shader_register_class_name(ShaderRegisterClass::shader_resource) == "shader_resource");
    CHECK(shader_register_class_name(ShaderRegisterClass::unordered_access) == "unordered_access");
    CHECK(shader_register_class_name(ShaderRegisterClass::sampler) == "sampler");

    CHECK(shader_profile(ShaderStage::vertex, "6.0") == "vs_6_0");
    CHECK(shader_profile(ShaderStage::fragment, "6.6") == "ps_6_6");
    CHECK(shader_profile(ShaderStage::compute, "6.9") == "cs_6_9");

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
    CHECK(reflection.valid);
    CHECK(reflection.diagnostics.empty());
    CHECK(reflection.resources.size() == 4U);

    const auto& constant_buffer = reflection.resources[0];
    CHECK(constant_buffer.name == "FrameData");
    CHECK(constant_buffer.register_class == ShaderRegisterClass::constant_buffer);
    CHECK(constant_buffer.binding == 2U);
    CHECK(constant_buffer.space == 1U);
    CHECK(constant_buffer.count == 1U);
    CHECK(!constant_buffer.unbounded);

    const auto& sampler = reflection.resources[1];
    CHECK(sampler.register_class == ShaderRegisterClass::sampler);
    CHECK(sampler.binding == 3U);
    CHECK(sampler.space == 1U);

    const auto& texture = reflection.resources[2];
    CHECK(texture.register_class == ShaderRegisterClass::shader_resource);
    CHECK(texture.binding == 4U);
    CHECK(texture.space == 2U);
    CHECK(texture.count == 6U);
    CHECK(texture.type == "texture");
    CHECK(texture.format == "f32");
    CHECK(texture.dimension == "2d");

    const auto& unordered_access = reflection.resources[3];
    CHECK(unordered_access.register_class == ShaderRegisterClass::unordered_access);
    CHECK(unordered_access.binding == 7U);
    CHECK(unordered_access.space == 3U);
    CHECK(unordered_access.count == 0U);
    CHECK(unordered_access.unbounded);

    constexpr const char* compact_unbounded_assembly = R"DXC(
;
; Resource Bindings:
;
; Name                                 Type  Format         Dim      ID      HLSL Bind  Count
; ------------------------------ ---------- ------- ----------- ------- -------------- ------
; InputBuffers                      texture  struct         r/o      T0     t0unbounded
;
; ViewId state:
)DXC";
    const ShaderReflection compact_unbounded_reflection =
        parse_dxc_resource_bindings(compact_unbounded_assembly);
    CHECK(compact_unbounded_reflection.valid);
    CHECK(compact_unbounded_reflection.diagnostics.empty());
    CHECK(compact_unbounded_reflection.resources.size() == 1U);
    const auto& compact_unbounded = compact_unbounded_reflection.resources[0];
    CHECK(compact_unbounded.name == "InputBuffers");
    CHECK(compact_unbounded.register_class == ShaderRegisterClass::shader_resource);
    CHECK(compact_unbounded.binding == 0U);
    CHECK(compact_unbounded.space == 0U);
    CHECK(compact_unbounded.count == 0U);
    CHECK(compact_unbounded.unbounded);

    constexpr const char* malformed_compact_unbounded_assembly = R"DXC(
;
; Resource Bindings:
;
; Name                                 Type  Format         Dim      ID      HLSL Bind  Count
; ------------------------------ ---------- ------- ----------- ------- -------------- ------
; InputBuffers                      texture  struct         r/o      T0       unbounded
;
)DXC";
    const ShaderReflection malformed_compact_unbounded_reflection =
        parse_dxc_resource_bindings(malformed_compact_unbounded_assembly);
    CHECK(!malformed_compact_unbounded_reflection.valid);
    CHECK(!malformed_compact_unbounded_reflection.diagnostics.empty());

    const ShaderReflection empty_reflection = parse_dxc_resource_bindings(R"DXC(
; Resource Bindings:
;
; Name                                 Type  Format         Dim      ID      HLSL Bind  Count
; ------------------------------ ---------- ------- ----------- ------- -------------- ------
;
)DXC");
    CHECK(empty_reflection.valid);
    CHECK(empty_reflection.resources.empty());

    const ShaderReflection malformed_reflection = parse_dxc_resource_bindings("; no bindings here\n");
    CHECK(!malformed_reflection.valid);
    CHECK(!malformed_reflection.diagnostics.empty());

    ShaderReflection bounded_reflection;
    bounded_reflection.valid = true;
    bounded_reflection.resources = {
        ShaderResourceReflection{
            .name = "FrameData",
            .register_class = ShaderRegisterClass::constant_buffer,
            .binding = 2,
            .space = 1,
            .count = 1,
            .unbounded = false,
            .type = "cbuffer",
            .format = "NA",
            .dimension = "NA",
        },
        ShaderResourceReflection{
            .name = "LinearSampler",
            .register_class = ShaderRegisterClass::sampler,
            .binding = 3,
            .space = 1,
            .count = 1,
            .unbounded = false,
            .type = "sampler",
            .format = "NA",
            .dimension = "NA",
        },
        ShaderResourceReflection{
            .name = "InputTexture",
            .register_class = ShaderRegisterClass::shader_resource,
            .binding = 4,
            .space = 2,
            .count = 2,
            .unbounded = false,
            .type = "texture",
            .format = "f32",
            .dimension = "2d",
        },
    };

    constexpr std::string_view metal_json = R"JSON({
        "EntryPoint": "main",
        "ShaderType": "Fragment",
        "TopLevelArgumentBuffer": [
            {"EltOffset": 0, "Size": 24, "Slot": 2, "Space": 1, "Type": "CBV"},
            {"EltOffset": 24, "Size": 24, "Slot": 3, "Space": 1, "Type": "SMP"},
            {"EltOffset": 48, "Size": 48, "Slot": 4, "Space": 2, "Type": "SRV"}
        ]
    })JSON";

    const MetalBindingReflection metal = parse_metal_shader_converter_reflection(
        metal_json,
        bounded_reflection
    );
    std::string converter3_json{metal_json};
    converter3_json.replace(converter3_json.find("SMP"),3,"Sampler");
    const auto converter3=parse_metal_shader_converter_reflection(converter3_json,bounded_reflection);
    CHECK(converter3.valid);
    CHECK(converter3.locations.size()==3U);
    CHECK(metal.valid);
    CHECK(metal.diagnostics.empty());
    CHECK(metal.locations.size() == 3U);
    CHECK(metal.locations[0].register_class == ShaderRegisterClass::constant_buffer);
    CHECK(metal.locations[0].argument_buffer_offset == 0U);
    CHECK(metal.locations[1].register_class == ShaderRegisterClass::sampler);
    CHECK(metal.locations[1].argument_buffer_offset == 24U);
    CHECK(metal.locations[2].register_class == ShaderRegisterClass::shader_resource);
    CHECK(metal.locations[2].binding == 4U);
    CHECK(metal.locations[2].space == 2U);
    CHECK(metal.locations[2].argument_buffer_offset == 48U);
    CHECK(metal.locations[2].size == 48U);

    constexpr std::string_view missing_sampler_json = R"JSON({
        "TopLevelArgumentBuffer": [
            {"EltOffset": 0, "Size": 24, "Slot": 2, "Space": 1, "Type": "CBV"},
            {"EltOffset": 24, "Size": 48, "Slot": 4, "Space": 2, "Type": "SRV"}
        ]
    })JSON";
    const MetalBindingReflection missing_sampler = parse_metal_shader_converter_reflection(
        missing_sampler_json,
        bounded_reflection
    );
    CHECK(!missing_sampler.valid);
    CHECK(missing_sampler.diagnostics.find("sampler") != std::string::npos);

    const MetalBindingReflection unbounded_metal = parse_metal_shader_converter_reflection(
        metal_json,
        reflection
    );
    CHECK(!unbounded_metal.valid);
    CHECK(unbounded_metal.diagnostics.find("unbounded") != std::string::npos);

    constexpr std::string_view explicit_json = R"JSON({
        "EntryPoint": "main",
        "ShaderType": "Compute",
        "TopLevelArgumentBuffer": [
            {"EltOffset": 16, "Size": 8, "Slot": 2, "Space": 1, "Type": "CBV"},
            {"EltOffset": 40, "Size": 8, "Slot": 3, "Space": 1, "Type": "SMP"},
            {"EltOffset": 64, "Size": 8, "Slot": 4, "Space": 2, "Type": "SRV"},
            {"EltOffset": 96, "Size": 8, "Slot": 7, "Space": 3, "Type": "UAV"}
        ]
    })JSON";
    const MetalBindingReflection explicit_metal =
        parse_explicit_metal_shader_converter_reflection(explicit_json, reflection);
    CHECK(explicit_metal.valid);
    CHECK(explicit_metal.diagnostics.empty());
    CHECK(explicit_metal.model == MetalBindingModel::explicit_descriptor_tables);
    CHECK(explicit_metal.locations.size() == reflection.resources.size());
    CHECK(explicit_metal.locations[0].argument_buffer_offset == 16U);
    CHECK(explicit_metal.locations[0].size == sizeof(std::uint64_t));
    CHECK(explicit_metal.locations[0].descriptor_table);
    CHECK(explicit_metal.locations[1].argument_buffer_offset == 40U);
    CHECK(explicit_metal.locations[2].argument_buffer_offset == 64U);
    CHECK(explicit_metal.locations[3].argument_buffer_offset == 96U);
    CHECK(explicit_metal.locations[3].descriptor_table);
    CHECK(explicit_metal.locations[3].register_class == ShaderRegisterClass::unordered_access);

    constexpr std::string_view explicit_table_json = R"JSON({
        "EntryPoint": "main",
        "ShaderType": "Compute",
        "TopLevelArgumentBuffer": [
            {"EltOffset": 16, "Size": 8, "Slot": 4294967295, "Type": "Table"},
            {"EltOffset": 40, "Size": 8, "Slot": 4294967295, "Type": "Table"},
            {"EltOffset": 64, "Size": 8, "Slot": 4294967295, "Type": "Table"},
            {"EltOffset": 96, "Size": 8, "Slot": 4294967295, "Type": "Table"}
        ]
    })JSON";
    const MetalBindingReflection explicit_table_metal =
        parse_explicit_metal_shader_converter_reflection(explicit_table_json, reflection);
    CHECK(explicit_table_metal.valid);
    CHECK(explicit_table_metal.diagnostics.empty());
    CHECK(explicit_table_metal.locations.size() == reflection.resources.size());
    CHECK(explicit_table_metal.locations[0].register_class == ShaderRegisterClass::constant_buffer);
    CHECK(explicit_table_metal.locations[0].binding == 2U);
    CHECK(explicit_table_metal.locations[1].register_class == ShaderRegisterClass::sampler);
    CHECK(explicit_table_metal.locations[1].binding == 3U);
    CHECK(explicit_table_metal.locations[2].register_class == ShaderRegisterClass::shader_resource);
    CHECK(explicit_table_metal.locations[2].binding == 4U);
    CHECK(explicit_table_metal.locations[3].register_class == ShaderRegisterClass::unordered_access);
    CHECK(explicit_table_metal.locations[3].binding == 7U);
    CHECK(explicit_table_metal.locations[3].descriptor_table);

    constexpr std::string_view malformed_explicit_json = R"JSON({
        "TopLevelArgumentBuffer": [
            {"EltOffset": 16, "Size": 24, "Slot": 4294967295, "Type": "Table"},
            {"EltOffset": 40, "Size": 8, "Slot": 4294967295, "Type": "Table"},
            {"EltOffset": 64, "Size": 8, "Slot": 4294967295, "Type": "Table"},
            {"EltOffset": 96, "Size": 8, "Slot": 4294967295, "Type": "Table"}
        ]
    })JSON";
    const MetalBindingReflection malformed_explicit =
        parse_explicit_metal_shader_converter_reflection(malformed_explicit_json, reflection);
    CHECK(!malformed_explicit.valid);
    CHECK(malformed_explicit.diagnostics.find("64-bit") != std::string::npos);

    const std::string explicit_root = build_metal_root_signature_json(
        ShaderStage::compute, reflection
    );
    CHECK(!explicit_root.empty());
    CHECK(explicit_root.find("IRRootSignatureVersion_1_1") != std::string::npos);
    CHECK(explicit_root.find("IRDescriptorRangeTypeUAV") != std::string::npos);
    CHECK(explicit_root.find("4294967295") != std::string::npos);
    CHECK(explicit_root.find("IRRootParameterTypeDescriptorTable") != std::string::npos);

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
    CHECK(first == second);
    CHECK(first.size() == 64U);

    request.entry_point = "other_vs";
    const auto changed_entry = make_cache_key(source, request, "dxc 1", "msc 1");
    CHECK(first != changed_entry);

    request.entry_point = "main_vs";
    request.debug = true;
    const auto debug_key = make_cache_key(source, request, "dxc 1", "msc 1");
    CHECK(first != debug_key);

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
    CHECK(discover_executable(fake_tool, "VICE_TEST_UNUSED", "vice-fake-tool") == fake_tool);
    std::filesystem::remove_all(temporary);

    return 0;
}

#undef CHECK
