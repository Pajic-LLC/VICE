#include <vice/render/rhi.hpp>

#include <cassert>
#include <memory>

int main() {
    using namespace vice::render;

    auto device = Device::create();
    if (!device) {
        return 0;
    }

    ShaderModuleDesc vertex_desc;
    vertex_desc.stage = ShaderStage::vertex;
    vertex_desc.entry_point = "vice_offscreen_vs";
    vertex_desc.debug_name = "VICE offscreen HDR smoke vertex";

    ShaderModuleDesc fragment_desc;
    fragment_desc.stage = ShaderStage::fragment;
    fragment_desc.entry_point = "vice_offscreen_fs";
    fragment_desc.debug_name = "VICE offscreen HDR smoke fragment";

    if (device->backend() == Backend::metal) {
        vertex_desc.language = ShaderSourceLanguage::metal;
        vertex_desc.source = R"(
#include <metal_stdlib>
using namespace metal;
vertex float4 vice_offscreen_vs(uint vertex_id [[vertex_id]]) {
    const float2 positions[3] = {
        float2(-0.5, -0.5),
        float2( 0.0,  0.5),
        float2( 0.5, -0.5),
    };
    return float4(positions[vertex_id], 0.5, 1.0);
}
)";
        fragment_desc.language = ShaderSourceLanguage::metal;
        fragment_desc.source = R"(
#include <metal_stdlib>
using namespace metal;
fragment float4 vice_offscreen_fs() {
    return float4(2.0, 0.75, 0.25, 1.0);
}
)";
    } else {
        vertex_desc.language = ShaderSourceLanguage::hlsl;
        vertex_desc.source = R"(
float4 vice_offscreen_vs(uint vertex_id : SV_VertexID) : SV_Position {
    const float2 positions[3] = {
        float2(-0.5, -0.5),
        float2( 0.0,  0.5),
        float2( 0.5, -0.5)
    };
    return float4(positions[vertex_id], 0.5, 1.0);
}
)";
        fragment_desc.language = ShaderSourceLanguage::hlsl;
        fragment_desc.source = R"(
float4 vice_offscreen_fs() : SV_Target0 {
    return float4(2.0, 0.75, 0.25, 1.0);
}
)";
    }

    auto vertex_shader = device->create_shader_module(vertex_desc);
    auto fragment_shader = device->create_shader_module(fragment_desc);
    assert(vertex_shader);
    assert(fragment_shader);

    GraphicsPipelineDesc pipeline_desc;
    pipeline_desc.vertex_shader = vertex_shader.get();
    pipeline_desc.fragment_shader = fragment_shader.get();
    pipeline_desc.color_format = TextureFormat::rgba16_float;
    pipeline_desc.cull_mode = CullMode::none;
    pipeline_desc.debug_name = "VICE offscreen HDR smoke pipeline";
    auto pipeline = device->create_graphics_pipeline(pipeline_desc);
    assert(pipeline);

    GraphicsPipelineDesc mismatch_desc = pipeline_desc;
    mismatch_desc.color_format = TextureFormat::bgra8_unorm;
    mismatch_desc.debug_name = "VICE offscreen format mismatch pipeline";
    auto mismatch_pipeline = device->create_graphics_pipeline(mismatch_desc);
    assert(mismatch_pipeline);

    TextureDesc color_desc;
    color_desc.width = 32U;
    color_desc.height = 32U;
    color_desc.format = TextureFormat::rgba16_float;
    color_desc.usage = TextureUsage::color_attachment | TextureUsage::sampled;
    color_desc.debug_name = "VICE offscreen HDR smoke texture";
    auto color = device->create_texture(color_desc);
    assert(color);

    auto commands = device->create_command_list();
    assert(commands);
    RenderPassDesc pass_desc;
    pass_desc.clear_color = Color{0.1F, 0.2F, 0.3F, 1.0F};
    assert(commands->begin_render_pass(*color, pass_desc));
    assert(!commands->set_graphics_pipeline(*mismatch_pipeline));
    assert(commands->set_graphics_pipeline(*pipeline));
    assert(commands->draw(3U));
    assert(commands->end_render_pass());

    const SubmissionId submission = device->submit(std::move(commands));
    assert(submission != invalid_submission);
    assert(device->wait_for_submission(submission));
    return 0;
}
