#include <vice/render/rhi.hpp>

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <span>

int main() {
    using namespace vice::render;

    static_assert(sizeof(IndexedIndirectDrawArguments) == 20U);
    static_assert(offsetof(IndexedIndirectDrawArguments, index_count) == 0U);
    static_assert(offsetof(IndexedIndirectDrawArguments, instance_count) == 4U);
    static_assert(offsetof(IndexedIndirectDrawArguments, first_index) == 8U);
    static_assert(offsetof(IndexedIndirectDrawArguments, vertex_offset) == 12U);
    static_assert(offsetof(IndexedIndirectDrawArguments, first_instance) == 16U);

    auto device = Device::create();
    if (!device) {
        return 0;
    }

    const std::array compute_resources{
        ShaderResourceBindingDesc{
            .register_class = ShaderRegisterClass::unordered_access,
            .binding = 0U,
            .space = 0U,
            .count = 1U,
            .unbounded = false,
        },
    };

    ShaderModuleDesc compute_desc;
    compute_desc.stage = ShaderStage::compute;
    compute_desc.resources = compute_resources;
    compute_desc.entry_point = "vice_write_indexed_indirect_args";
    compute_desc.debug_name = "VICE indexed indirect argument writer";

    ShaderModuleDesc vertex_desc;
    vertex_desc.stage = ShaderStage::vertex;
    vertex_desc.entry_point = "vice_indexed_indirect_vs";
    vertex_desc.debug_name = "VICE indexed indirect smoke vertex";

    ShaderModuleDesc fragment_desc;
    fragment_desc.stage = ShaderStage::fragment;
    fragment_desc.entry_point = "vice_indexed_indirect_fs";
    fragment_desc.debug_name = "VICE indexed indirect smoke fragment";

    if (device->backend() == Backend::metal) {
        compute_desc.language = ShaderSourceLanguage::metal;
        compute_desc.source = R"(
#include <metal_stdlib>
using namespace metal;
kernel void vice_write_indexed_indirect_args(device uint* arguments [[buffer(0)]]) {
    arguments[0] = 3U;
    arguments[1] = 1U;
    arguments[2] = 0U;
    arguments[3] = 0U;
    arguments[4] = 0U;
}
)";
        vertex_desc.language = ShaderSourceLanguage::metal;
        vertex_desc.source = R"(
#include <metal_stdlib>
using namespace metal;
vertex float4 vice_indexed_indirect_vs(uint vertex_id [[vertex_id]]) {
    const float2 positions[3] = {
        float2(-0.6, -0.5),
        float2( 0.0,  0.6),
        float2( 0.6, -0.5),
    };
    return float4(positions[vertex_id], 0.5, 1.0);
}
)";
        fragment_desc.language = ShaderSourceLanguage::metal;
        fragment_desc.source = R"(
#include <metal_stdlib>
using namespace metal;
fragment float4 vice_indexed_indirect_fs() {
    return float4(0.25, 1.5, 0.5, 1.0);
}
)";
    } else {
        compute_desc.language = ShaderSourceLanguage::hlsl;
        compute_desc.source = R"(
RWStructuredBuffer<uint> arguments : register(u0);
[numthreads(1, 1, 1)]
void vice_write_indexed_indirect_args() {
    arguments[0] = 3U;
    arguments[1] = 1U;
    arguments[2] = 0U;
    arguments[3] = 0U;
    arguments[4] = 0U;
}
)";
        vertex_desc.language = ShaderSourceLanguage::hlsl;
        vertex_desc.source = R"(
float4 vice_indexed_indirect_vs(uint vertex_id : SV_VertexID) : SV_Position {
    const float2 positions[3] = {
        float2(-0.6, -0.5),
        float2( 0.0,  0.6),
        float2( 0.6, -0.5)
    };
    return float4(positions[vertex_id], 0.5, 1.0);
}
)";
        fragment_desc.language = ShaderSourceLanguage::hlsl;
        fragment_desc.source = R"(
float4 vice_indexed_indirect_fs() : SV_Target0 {
    return float4(0.25, 1.5, 0.5, 1.0);
}
)";
    }

    auto compute_shader = device->create_shader_module(compute_desc);
    auto vertex_shader = device->create_shader_module(vertex_desc);
    auto fragment_shader = device->create_shader_module(fragment_desc);
    assert(compute_shader);
    assert(vertex_shader);
    assert(fragment_shader);

    ComputePipelineDesc compute_pipeline_desc;
    compute_pipeline_desc.compute_shader = compute_shader.get();
    compute_pipeline_desc.thread_group_size = {1U, 1U, 1U};
    compute_pipeline_desc.debug_name = "VICE indexed indirect writer pipeline";
    auto compute_pipeline = device->create_compute_pipeline(compute_pipeline_desc);
    assert(compute_pipeline);

    GraphicsPipelineDesc graphics_pipeline_desc;
    graphics_pipeline_desc.vertex_shader = vertex_shader.get();
    graphics_pipeline_desc.fragment_shader = fragment_shader.get();
    graphics_pipeline_desc.color_format = TextureFormat::rgba16_float;
    graphics_pipeline_desc.cull_mode = CullMode::none;
    graphics_pipeline_desc.debug_name = "VICE indexed indirect smoke pipeline";
    auto graphics_pipeline = device->create_graphics_pipeline(graphics_pipeline_desc);
    assert(graphics_pipeline);

    BufferDesc indirect_desc;
    indirect_desc.size = sizeof(IndexedIndirectDrawArguments);
    indirect_desc.usage = BufferUsage::storage | BufferUsage::indirect;
    indirect_desc.memory = MemoryLocation::gpu_only;
    indirect_desc.debug_name = "VICE GPU-written indexed indirect args";
    auto indirect = device->create_buffer(indirect_desc);
    assert(indirect);

    const std::array<std::uint32_t, 3U> indices{0U, 1U, 2U};
    BufferDesc index_desc;
    index_desc.size = sizeof(indices);
    index_desc.usage = BufferUsage::index;
    index_desc.memory = MemoryLocation::cpu_to_gpu;
    index_desc.debug_name = "VICE indexed indirect smoke indices";
    auto index = device->create_buffer(index_desc);
    assert(index);
    assert(index->write(0U, std::as_bytes(std::span{indices})));

    TextureDesc color_desc;
    color_desc.width = 32U;
    color_desc.height = 32U;
    color_desc.format = TextureFormat::rgba16_float;
    color_desc.usage = TextureUsage::color_attachment | TextureUsage::sampled;
    color_desc.debug_name = "VICE indexed indirect smoke color";
    auto color = device->create_texture(color_desc);
    assert(color);

    auto commands = device->create_command_list();
    assert(commands);
    assert(!commands->draw_indexed_indirect(*indirect));
    assert(commands->set_compute_pipeline(*compute_pipeline));
    assert(commands->set_compute_storage_buffer(
        ShaderBindingKey{
            .register_class = ShaderRegisterClass::unordered_access,
            .binding = 0U,
            .space = 0U,
            .array_element = 0U,
        },
        *indirect,
        BufferViewDesc{
            .offset = 0U,
            .size = sizeof(IndexedIndirectDrawArguments),
            .structure_stride = sizeof(std::uint32_t),
        }
    ));
    assert(commands->dispatch(ComputeDispatchDesc{}));

    RenderPassDesc pass;
    assert(commands->begin_render_pass(*color, pass));
    assert(!commands->draw_indexed_indirect(*indirect));
    assert(commands->set_graphics_pipeline(*graphics_pipeline));
    assert(!commands->draw_indexed_indirect(*indirect));
    assert(commands->set_index_buffer(*index, IndexFormat::uint32));
    assert(!commands->draw_indexed_indirect(*index));
    assert(!commands->draw_indexed_indirect(*indirect, 2U));
    assert(!commands->draw_indexed_indirect(*indirect, 4U));
    assert(commands->draw_indexed_indirect(*indirect));
    assert(commands->end_render_pass());

    const SubmissionId submission = device->submit(std::move(commands));
    assert(submission != invalid_submission);
    assert(device->wait_for_submission(submission));
    return 0;
}
