#include <vice/render/rhi.hpp>

#include <cassert>
#include <memory>

int main() {
    using namespace vice::render;

    auto device = Device::create();
    if (!device) {
        // Hosted runners are not guaranteed to expose a native graphics adapter.
        // Descriptor and pipeline validation still runs in vice_render_tests.
        return 0;
    }

    ShaderModuleDesc vertex_desc;
    vertex_desc.stage = ShaderStage::vertex;
    vertex_desc.entry_point = "vice_depth_only_vs";
    vertex_desc.debug_name = "VICE depth-only render-pass smoke vertex";

    if (device->backend() == Backend::metal) {
        vertex_desc.language = ShaderSourceLanguage::metal;
        vertex_desc.source = R"(
#include <metal_stdlib>
using namespace metal;
vertex float4 vice_depth_only_vs(uint vertex_id [[vertex_id]]) {
    const float2 positions[3] = {
        float2(-0.5, -0.5),
        float2( 0.0,  0.5),
        float2( 0.5, -0.5),
    };
    return float4(positions[vertex_id], 0.5, 1.0);
}
)";
    } else {
        vertex_desc.language = ShaderSourceLanguage::hlsl;
        vertex_desc.source = R"(
float4 vice_depth_only_vs(uint vertex_id : SV_VertexID) : SV_Position {
    const float2 positions[3] = {
        float2(-0.5, -0.5),
        float2( 0.0,  0.5),
        float2( 0.5, -0.5)
    };
    return float4(positions[vertex_id], 0.5, 1.0);
}
)";
    }

    auto vertex_shader = device->create_shader_module(vertex_desc);
    assert(vertex_shader);

    GraphicsPipelineDesc pipeline_desc;
    pipeline_desc.vertex_shader = vertex_shader.get();
    pipeline_desc.fragment_shader = nullptr;
    pipeline_desc.color_format = TextureFormat::undefined;
    pipeline_desc.cull_mode = CullMode::none;
    pipeline_desc.depth.format = TextureFormat::depth32_float;
    pipeline_desc.depth.test_enabled = true;
    pipeline_desc.depth.write_enabled = true;
    pipeline_desc.depth.compare = CompareOp::less;
    pipeline_desc.debug_name = "VICE depth-only render-pass smoke pipeline";
    auto pipeline = device->create_graphics_pipeline(pipeline_desc);
    assert(pipeline);

    TextureDesc depth_desc;
    depth_desc.width = 32U;
    depth_desc.height = 32U;
    depth_desc.format = TextureFormat::depth32_float;
    depth_desc.usage = TextureUsage::depth_attachment | TextureUsage::sampled;
    depth_desc.debug_name = "VICE depth-only render-pass smoke texture";
    auto depth = device->create_texture(depth_desc);
    assert(depth);

    auto commands = device->create_command_list();
    assert(commands);
    assert(commands->begin_depth_render_pass(*depth, 1.0F, true));
    assert(commands->set_viewport(Viewport{.width = 32.0F, .height = 32.0F}));
    assert(commands->set_scissor_rect(ScissorRect{.width = 32U, .height = 32U}));
    assert(commands->set_graphics_pipeline(*pipeline));
    assert(commands->draw(3U));
    assert(commands->end_render_pass());

    const SubmissionId submission = device->submit(std::move(commands));
    assert(submission != invalid_submission);
    assert(device->wait_for_submission(submission));
    return 0;
}
