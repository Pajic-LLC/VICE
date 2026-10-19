#include <vice/render/rhi.hpp>

#include <array>
#include <cassert>
#include <cstddef>
#include <memory>
#include <span>

namespace {

struct ToneMapConstants {
    float exposure = 1.0F;
    float padding[3]{};
};

constexpr vice::render::ShaderBindingKey tone_constants_binding{
    .register_class = vice::render::ShaderRegisterClass::constant_buffer,
    .binding = 0,
    .space = 0,
    .array_element = 0,
};
constexpr vice::render::ShaderBindingKey tone_texture_binding{
    .register_class = vice::render::ShaderRegisterClass::shader_resource,
    .binding = 0,
    .space = 0,
    .array_element = 0,
};
constexpr vice::render::ShaderBindingKey tone_sampler_binding{
    .register_class = vice::render::ShaderRegisterClass::sampler,
    .binding = 0,
    .space = 0,
    .array_element = 0,
};

static constexpr std::array tone_resources{
    vice::render::ShaderResourceBindingDesc{
        .register_class = vice::render::ShaderRegisterClass::constant_buffer,
        .binding = 0,
        .space = 0,
        .count = 1,
        .unbounded = false,
    },
    vice::render::ShaderResourceBindingDesc{
        .register_class = vice::render::ShaderRegisterClass::shader_resource,
        .binding = 0,
        .space = 0,
        .count = 1,
        .unbounded = false,
    },
    vice::render::ShaderResourceBindingDesc{
        .register_class = vice::render::ShaderRegisterClass::sampler,
        .binding = 0,
        .space = 0,
        .count = 1,
        .unbounded = false,
    },
};

} // namespace

int main() {
    using namespace vice::render;

    auto device = Device::create();
    if (!device) return 0;

    const bool metal = device->backend() == Backend::metal;

    ShaderModuleDesc hdr_vertex_desc;
    hdr_vertex_desc.stage = ShaderStage::vertex;
    hdr_vertex_desc.language = metal ? ShaderSourceLanguage::metal : ShaderSourceLanguage::hlsl;
    hdr_vertex_desc.entry_point = "vice_hdr_vs";
    hdr_vertex_desc.debug_name = "VICE HDR tone-map smoke vertex";
    hdr_vertex_desc.source = metal ? R"(
#include <metal_stdlib>
using namespace metal;
vertex float4 vice_hdr_vs(uint vertex_id [[vertex_id]]) {
    constexpr float2 positions[3] = {
        float2(-0.5, -0.5),
        float2( 0.0,  0.5),
        float2( 0.5, -0.5),
    };
    return float4(positions[vertex_id], 0.5, 1.0);
}
)" : R"(
float4 vice_hdr_vs(uint vertex_id : SV_VertexID) : SV_Position {
    const float2 positions[3] = {
        float2(-0.5, -0.5),
        float2( 0.0,  0.5),
        float2( 0.5, -0.5)
    };
    return float4(positions[vertex_id], 0.5, 1.0);
}
)";

    ShaderModuleDesc hdr_fragment_desc;
    hdr_fragment_desc.stage = ShaderStage::fragment;
    hdr_fragment_desc.language = hdr_vertex_desc.language;
    hdr_fragment_desc.entry_point = "vice_hdr_fs";
    hdr_fragment_desc.debug_name = "VICE HDR tone-map smoke fragment";
    hdr_fragment_desc.source = metal ? R"(
#include <metal_stdlib>
using namespace metal;
fragment float4 vice_hdr_fs() {
    return float4(4.0, 1.5, 0.25, 1.0);
}
)" : R"(
float4 vice_hdr_fs() : SV_Target0 {
    return float4(4.0, 1.5, 0.25, 1.0);
}
)";

    ShaderModuleDesc tone_vertex_desc;
    tone_vertex_desc.stage = ShaderStage::vertex;
    tone_vertex_desc.language = hdr_vertex_desc.language;
    tone_vertex_desc.entry_point = "vice_tone_vs";
    tone_vertex_desc.debug_name = "VICE tone-map smoke vertex";
    tone_vertex_desc.source = metal ? R"(
#include <metal_stdlib>
using namespace metal;
struct Out { float4 position [[position]]; float2 uv; };
vertex Out vice_tone_vs(uint vertex_id [[vertex_id]]) {
    constexpr float2 uvs[3] = {float2(0.0, 0.0), float2(2.0, 0.0), float2(0.0, 2.0)};
    Out output;
    output.uv = uvs[vertex_id];
    output.position = float4(output.uv.x * 2.0 - 1.0, 1.0 - output.uv.y * 2.0, 0.0, 1.0);
    return output;
}
)" : R"(
struct Out { float4 position : SV_Position; float2 uv : TEXCOORD0; };
Out vice_tone_vs(uint vertex_id : SV_VertexID) {
    const float2 uvs[3] = {float2(0.0, 0.0), float2(2.0, 0.0), float2(0.0, 2.0)};
    Out output;
    output.uv = uvs[vertex_id];
    output.position = float4(output.uv.x * 2.0 - 1.0, 1.0 - output.uv.y * 2.0, 0.0, 1.0);
    return output;
}
)";

    ShaderModuleDesc tone_fragment_desc;
    tone_fragment_desc.stage = ShaderStage::fragment;
    tone_fragment_desc.language = hdr_vertex_desc.language;
    tone_fragment_desc.resources = tone_resources;
    tone_fragment_desc.entry_point = "vice_tone_fs";
    tone_fragment_desc.debug_name = "VICE tone-map smoke fragment";
    tone_fragment_desc.source = metal ? R"(
#include <metal_stdlib>
using namespace metal;
struct Frame { float4 tone_map_data; };
struct In { float4 position [[position]]; float2 uv; };
fragment float4 vice_tone_fs(
    In input [[stage_in]],
    constant Frame& frame [[buffer(0)]],
    texture2d<float> scene_color [[texture(0)]],
    sampler scene_sampler [[sampler(0)]]) {
    const float3 hdr = scene_color.sample(scene_sampler, input.uv).rgb * frame.tone_map_data.x;
    const float3 mapped = clamp((hdr * (2.51 * hdr + 0.03)) / (hdr * (2.43 * hdr + 0.59) + 0.14), 0.0, 1.0);
    return float4(mapped, 1.0);
}
)" : R"(
cbuffer Frame : register(b0) { float4 tone_map_data; };
Texture2D<float4> scene_color : register(t0);
SamplerState scene_sampler : register(s0);
struct In { float4 position : SV_Position; float2 uv : TEXCOORD0; };
float4 vice_tone_fs(In input) : SV_Target0 {
    const float3 hdr = scene_color.Sample(scene_sampler, input.uv).rgb * tone_map_data.x;
    const float3 mapped = saturate((hdr * (2.51 * hdr + 0.03)) / (hdr * (2.43 * hdr + 0.59) + 0.14));
    return float4(mapped, 1.0);
}
)";

    auto hdr_vertex = device->create_shader_module(hdr_vertex_desc);
    auto hdr_fragment = device->create_shader_module(hdr_fragment_desc);
    auto tone_vertex = device->create_shader_module(tone_vertex_desc);
    auto tone_fragment = device->create_shader_module(tone_fragment_desc);
    assert(hdr_vertex && hdr_fragment && tone_vertex && tone_fragment);

    GraphicsPipelineDesc hdr_pipeline_desc;
    hdr_pipeline_desc.vertex_shader = hdr_vertex.get();
    hdr_pipeline_desc.fragment_shader = hdr_fragment.get();
    hdr_pipeline_desc.color_format = TextureFormat::rgba16_float;
    hdr_pipeline_desc.cull_mode = CullMode::none;
    auto hdr_pipeline = device->create_graphics_pipeline(hdr_pipeline_desc);
    assert(hdr_pipeline);

    GraphicsPipelineDesc tone_pipeline_desc;
    tone_pipeline_desc.vertex_shader = tone_vertex.get();
    tone_pipeline_desc.fragment_shader = tone_fragment.get();
    tone_pipeline_desc.color_format = TextureFormat::bgra8_unorm;
    tone_pipeline_desc.cull_mode = CullMode::none;
    auto tone_pipeline = device->create_graphics_pipeline(tone_pipeline_desc);
    assert(tone_pipeline);

    TextureDesc hdr_desc;
    hdr_desc.width = 32U;
    hdr_desc.height = 32U;
    hdr_desc.format = TextureFormat::rgba16_float;
    hdr_desc.usage = TextureUsage::color_attachment | TextureUsage::sampled;
    auto hdr_target = device->create_texture(hdr_desc);
    assert(hdr_target);

    TextureDesc output_desc;
    output_desc.width = 32U;
    output_desc.height = 32U;
    output_desc.format = TextureFormat::bgra8_unorm;
    output_desc.usage = TextureUsage::color_attachment;
    auto output = device->create_texture(output_desc);
    assert(output);

    BufferDesc constants_desc;
    constants_desc.size = sizeof(ToneMapConstants);
    constants_desc.usage = BufferUsage::uniform;
    constants_desc.memory = MemoryLocation::cpu_to_gpu;
    auto constants = device->create_buffer(constants_desc);
    assert(constants);
    const ToneMapConstants tone_constants{.exposure = 1.25F};
    assert(constants->write(0U, std::as_bytes(std::span{&tone_constants, 1})));

    SamplerDesc sampler_desc;
    sampler_desc.min_filter = Filter::linear;
    sampler_desc.mag_filter = Filter::linear;
    sampler_desc.mip_filter = Filter::linear;
    sampler_desc.address_u = AddressMode::clamp_to_edge;
    sampler_desc.address_v = AddressMode::clamp_to_edge;
    sampler_desc.address_w = AddressMode::clamp_to_edge;
    auto sampler = device->create_sampler(sampler_desc);
    assert(sampler);

    auto commands = device->create_command_list();
    assert(commands);
    RenderPassDesc hdr_pass;
    hdr_pass.clear_color = Color{0.0F, 0.0F, 0.0F, 1.0F};
    assert(commands->begin_render_pass(*hdr_target, hdr_pass));
    assert(commands->set_graphics_pipeline(*hdr_pipeline));
    assert(commands->draw(3U));
    assert(commands->end_render_pass());

    RenderPassDesc tone_pass;
    tone_pass.clear_color = Color{0.0F, 0.0F, 0.0F, 1.0F};
    assert(commands->begin_render_pass(*output, tone_pass));
    assert(commands->set_graphics_pipeline(*tone_pipeline));
    assert(commands->set_graphics_constant_buffer(
        tone_constants_binding, *constants, 0U, sizeof(ToneMapConstants)));
    assert(commands->set_graphics_texture(tone_texture_binding, *hdr_target));
    assert(commands->set_graphics_sampler(tone_sampler_binding, *sampler));
    assert(commands->draw(3U));
    assert(commands->end_render_pass());

    const SubmissionId submission = device->submit(std::move(commands));
    assert(submission != invalid_submission);
    assert(device->wait_for_submission(submission));
    return 0;
}
