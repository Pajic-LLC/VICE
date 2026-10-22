#include <vice/math/math.hpp>
#include <vice/render/rhi.hpp>
#include <vice/runtime/gpu_draw_batches.hpp>
#include <vice/runtime/gpu_instance_table.hpp>
#include <vice/runtime/gpu_visibility_set.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <span>
#include <string>

namespace {

using namespace vice;

struct ShadowFrame {
    math::Mat4 view_projection{};
};

struct MotionFrame {
    math::Mat4 current_view_projection{};
    math::Mat4 previous_view_projection{};
};

static_assert(sizeof(ShadowFrame) == 64U);
static_assert(sizeof(MotionFrame) == 128U);

constexpr render::ShaderBindingKey frame_binding{
    .register_class = render::ShaderRegisterClass::constant_buffer,
    .binding = 0U,
    .space = 0U,
    .array_element = 0U,
};
constexpr render::ShaderBindingKey current_models_binding{
    .register_class = render::ShaderRegisterClass::shader_resource,
    .binding = 1U,
    .space = 0U,
    .array_element = 0U,
};
constexpr render::ShaderBindingKey shadow_compacted_binding{
    .register_class = render::ShaderRegisterClass::shader_resource,
    .binding = 2U,
    .space = 0U,
    .array_element = 0U,
};
constexpr render::ShaderBindingKey previous_models_binding{
    .register_class = render::ShaderRegisterClass::shader_resource,
    .binding = 2U,
    .space = 0U,
    .array_element = 0U,
};
constexpr render::ShaderBindingKey motion_compacted_binding{
    .register_class = render::ShaderRegisterClass::shader_resource,
    .binding = 3U,
    .space = 0U,
    .array_element = 0U,
};

[[nodiscard]] std::unique_ptr<render::Buffer> make_uniform(
    render::Device& device,
    const std::uint64_t size,
    const char* const debug_name
) {
    render::BufferDesc desc;
    desc.size = size;
    desc.usage = render::BufferUsage::uniform;
    desc.memory = render::MemoryLocation::cpu_to_gpu;
    desc.debug_name = debug_name;
    return device.create_buffer(desc);
}

[[nodiscard]] bool run_supporting_pass_execution_smoke() {
    using namespace vice::runtime;

    auto device = render::Device::create();
    if (!device) {
        // Some hosted environments do not expose a native Tier-1 device.
        return true;
    }

    RuntimeGpuIdentityVisibility visibility;
    RuntimeGpuDrawBatcher batches;
    RuntimeGpuInstanceTable current_models;
    RuntimeGpuInstanceTable previous_models;
    std::string diagnostics;
    if (!visibility.initialize(*device, &diagnostics) ||
        !batches.initialize(*device, &diagnostics) ||
        !current_models.initialize(*device, &diagnostics) ||
        !previous_models.initialize(*device, &diagnostics)) {
        std::cerr << "Could not initialize supporting-pass GPU state: " << diagnostics << '\n';
        return false;
    }

    const std::array<math::Mat4, 1U> current{math::identity()};
    const std::array<math::Mat4, 1U> previous{
        math::translation({-0.15F, 0.0F, 0.0F})
    };
    const std::array<std::uint32_t, 1U> assignments{0U};
    const std::array draw_batches{
        RuntimeGpuDrawBatch{
            .index_count = 3U,
            .first_index = 0U,
            .vertex_offset = 0,
        },
    };

    auto shadow_frame = make_uniform(*device, sizeof(ShadowFrame), "VICE supporting-pass shadow frame");
    auto motion_frame = make_uniform(*device, sizeof(MotionFrame), "VICE supporting-pass motion frame");
    if (!shadow_frame || !motion_frame) {
        std::cerr << "Could not create supporting-pass frame buffers.\n";
        return false;
    }
    const ShadowFrame shadow_constants{.view_projection = math::identity()};
    const MotionFrame motion_constants{
        .current_view_projection = math::identity(),
        .previous_view_projection = math::identity(),
    };
    if (!shadow_frame->write(0U, std::as_bytes(std::span{&shadow_constants, 1})) ||
        !motion_frame->write(0U, std::as_bytes(std::span{&motion_constants, 1}))) {
        std::cerr << "Could not stage supporting-pass frame constants.\n";
        return false;
    }

    const std::array shadow_resources{
        render::ShaderResourceBindingDesc{
            .register_class = render::ShaderRegisterClass::constant_buffer,
            .binding = 0U,
            .space = 0U,
            .count = 1U,
            .unbounded = false,
        },
        render::ShaderResourceBindingDesc{
            .register_class = render::ShaderRegisterClass::shader_resource,
            .binding = 1U,
            .space = 0U,
            .count = 1U,
            .unbounded = false,
        },
        render::ShaderResourceBindingDesc{
            .register_class = render::ShaderRegisterClass::shader_resource,
            .binding = 2U,
            .space = 0U,
            .count = 1U,
            .unbounded = false,
        },
    };
    const std::array motion_resources{
        render::ShaderResourceBindingDesc{
            .register_class = render::ShaderRegisterClass::constant_buffer,
            .binding = 0U,
            .space = 0U,
            .count = 1U,
            .unbounded = false,
        },
        render::ShaderResourceBindingDesc{
            .register_class = render::ShaderRegisterClass::shader_resource,
            .binding = 1U,
            .space = 0U,
            .count = 1U,
            .unbounded = false,
        },
        render::ShaderResourceBindingDesc{
            .register_class = render::ShaderRegisterClass::shader_resource,
            .binding = 2U,
            .space = 0U,
            .count = 1U,
            .unbounded = false,
        },
        render::ShaderResourceBindingDesc{
            .register_class = render::ShaderRegisterClass::shader_resource,
            .binding = 3U,
            .space = 0U,
            .count = 1U,
            .unbounded = false,
        },
    };

    render::ShaderModuleDesc shadow_vs_desc;
    shadow_vs_desc.stage = render::ShaderStage::vertex;
    shadow_vs_desc.resources = shadow_resources;
    shadow_vs_desc.entry_point = "vice_supporting_shadow_vs";
    shadow_vs_desc.debug_name = "VICE supporting-pass shadow execution vertex";

    render::ShaderModuleDesc motion_vs_desc;
    motion_vs_desc.stage = render::ShaderStage::vertex;
    motion_vs_desc.resources = motion_resources;
    motion_vs_desc.entry_point = "vice_supporting_motion_vs";
    motion_vs_desc.debug_name = "VICE supporting-pass motion execution vertex";

    render::ShaderModuleDesc motion_fs_desc;
    motion_fs_desc.stage = render::ShaderStage::fragment;
    motion_fs_desc.entry_point = "vice_supporting_motion_fs";
    motion_fs_desc.debug_name = "VICE supporting-pass motion execution fragment";

    if (device->backend() == render::Backend::metal) {
        shadow_vs_desc.language = render::ShaderSourceLanguage::metal;
        shadow_vs_desc.source = R"(
#include <metal_stdlib>
using namespace metal;
struct ShadowFrame { float4x4 view_projection; };
vertex float4 vice_supporting_shadow_vs(
    uint vertex_id [[vertex_id]],
    uint instance_id [[instance_id]],
    constant ShadowFrame& frame [[buffer(0)]],
    device const float4x4* models [[buffer(1)]],
    device const uint* compacted [[buffer(2)]]) {
    const float2 positions[3] = {
        float2(-0.6, -0.5), float2(0.0, 0.6), float2(0.6, -0.5)
    };
    const uint scene_instance = compacted[instance_id];
    return frame.view_projection * models[scene_instance] *
        float4(positions[vertex_id], 0.5, 1.0);
}
)";
        motion_vs_desc.language = render::ShaderSourceLanguage::metal;
        motion_vs_desc.source = R"(
#include <metal_stdlib>
using namespace metal;
struct MotionFrame {
    float4x4 current_view_projection;
    float4x4 previous_view_projection;
};
struct MotionOut {
    float4 position [[position]];
    float4 current_clip;
    float4 previous_clip;
};
vertex MotionOut vice_supporting_motion_vs(
    uint vertex_id [[vertex_id]],
    uint instance_id [[instance_id]],
    constant MotionFrame& frame [[buffer(0)]],
    device const float4x4* current_models [[buffer(1)]],
    device const float4x4* previous_models [[buffer(2)]],
    device const uint* compacted [[buffer(3)]]) {
    const float2 positions[3] = {
        float2(-0.6, -0.5), float2(0.0, 0.6), float2(0.6, -0.5)
    };
    const uint scene_instance = compacted[instance_id];
    const float4 local_position = float4(positions[vertex_id], 0.5, 1.0);
    MotionOut output;
    output.current_clip = frame.current_view_projection * current_models[scene_instance] * local_position;
    output.previous_clip = frame.previous_view_projection * previous_models[scene_instance] * local_position;
    output.position = output.current_clip;
    return output;
}
)";
        motion_fs_desc.language = render::ShaderSourceLanguage::metal;
        motion_fs_desc.source = R"(
#include <metal_stdlib>
using namespace metal;
struct MotionOut {
    float4 position [[position]];
    float4 current_clip;
    float4 previous_clip;
};
fragment float4 vice_supporting_motion_fs(MotionOut input [[stage_in]]) {
    const float2 current_ndc = input.current_clip.xy / input.current_clip.w;
    const float2 previous_ndc = input.previous_clip.xy / input.previous_clip.w;
    return float4((current_ndc - previous_ndc) * float2(0.5, -0.5), 0.0, 1.0);
}
)";
    } else {
        shadow_vs_desc.language = render::ShaderSourceLanguage::hlsl;
        shadow_vs_desc.source = R"(
cbuffer ShadowFrame : register(b0) { float4x4 view_projection; };
StructuredBuffer<float4x4> models : register(t1);
StructuredBuffer<uint> compacted : register(t2);
float4 vice_supporting_shadow_vs(uint vertex_id : SV_VertexID, uint instance_id : SV_InstanceID) : SV_Position {
    const float2 positions[3] = {
        float2(-0.6, -0.5), float2(0.0, 0.6), float2(0.6, -0.5)
    };
    const uint scene_instance = compacted[instance_id];
    const float4 world_position = mul(models[scene_instance], float4(positions[vertex_id], 0.5, 1.0));
    return mul(view_projection, world_position);
}
)";
        motion_vs_desc.language = render::ShaderSourceLanguage::hlsl;
        motion_vs_desc.source = R"(
cbuffer MotionFrame : register(b0) {
    float4x4 current_view_projection;
    float4x4 previous_view_projection;
};
StructuredBuffer<float4x4> current_models : register(t1);
StructuredBuffer<float4x4> previous_models : register(t2);
StructuredBuffer<uint> compacted : register(t3);
struct MotionOut {
    float4 position : SV_Position;
    float4 current_clip : TEXCOORD0;
    float4 previous_clip : TEXCOORD1;
};
MotionOut vice_supporting_motion_vs(uint vertex_id : SV_VertexID, uint instance_id : SV_InstanceID) {
    const float2 positions[3] = {
        float2(-0.6, -0.5), float2(0.0, 0.6), float2(0.6, -0.5)
    };
    const uint scene_instance = compacted[instance_id];
    const float4 local_position = float4(positions[vertex_id], 0.5, 1.0);
    MotionOut output;
    output.current_clip = mul(current_view_projection, mul(current_models[scene_instance], local_position));
    output.previous_clip = mul(previous_view_projection, mul(previous_models[scene_instance], local_position));
    output.position = output.current_clip;
    return output;
}
)";
        motion_fs_desc.language = render::ShaderSourceLanguage::hlsl;
        motion_fs_desc.source = R"(
struct MotionOut {
    float4 position : SV_Position;
    float4 current_clip : TEXCOORD0;
    float4 previous_clip : TEXCOORD1;
};
float4 vice_supporting_motion_fs(MotionOut input) : SV_Target0 {
    const float2 current_ndc = input.current_clip.xy / input.current_clip.w;
    const float2 previous_ndc = input.previous_clip.xy / input.previous_clip.w;
    return float4((current_ndc - previous_ndc) * float2(0.5, -0.5), 0.0, 1.0);
}
)";
    }

    auto shadow_vs = device->create_shader_module(shadow_vs_desc);
    auto motion_vs = device->create_shader_module(motion_vs_desc);
    auto motion_fs = device->create_shader_module(motion_fs_desc);
    if (!shadow_vs || !motion_vs || !motion_fs) {
        std::cerr << "Could not create supporting-pass execution shaders.\n";
        return false;
    }

    render::GraphicsPipelineDesc shadow_pipeline_desc;
    shadow_pipeline_desc.vertex_shader = shadow_vs.get();
    shadow_pipeline_desc.fragment_shader = nullptr;
    shadow_pipeline_desc.color_format = render::TextureFormat::undefined;
    shadow_pipeline_desc.cull_mode = render::CullMode::none;
    shadow_pipeline_desc.depth.format = render::TextureFormat::depth32_float;
    shadow_pipeline_desc.depth.test_enabled = true;
    shadow_pipeline_desc.depth.write_enabled = true;
    shadow_pipeline_desc.depth.compare = render::CompareOp::less;
    shadow_pipeline_desc.debug_name = "VICE supporting-pass shadow execution pipeline";
    auto shadow_pipeline = device->create_graphics_pipeline(shadow_pipeline_desc);

    render::GraphicsPipelineDesc motion_pipeline_desc;
    motion_pipeline_desc.vertex_shader = motion_vs.get();
    motion_pipeline_desc.fragment_shader = motion_fs.get();
    motion_pipeline_desc.color_format = render::TextureFormat::rgba16_float;
    motion_pipeline_desc.cull_mode = render::CullMode::none;
    motion_pipeline_desc.debug_name = "VICE supporting-pass motion execution pipeline";
    auto motion_pipeline = device->create_graphics_pipeline(motion_pipeline_desc);
    if (!shadow_pipeline || !motion_pipeline) {
        std::cerr << "Could not create supporting-pass execution pipelines.\n";
        return false;
    }

    const std::array<std::uint32_t, 3U> indices{0U, 1U, 2U};
    render::BufferDesc index_desc;
    index_desc.size = sizeof(indices);
    index_desc.usage = render::BufferUsage::index;
    index_desc.memory = render::MemoryLocation::cpu_to_gpu;
    index_desc.debug_name = "VICE supporting-pass execution indices";
    auto index_buffer = device->create_buffer(index_desc);
    if (!index_buffer || !index_buffer->write(0U, std::as_bytes(std::span{indices}))) {
        std::cerr << "Could not create supporting-pass index buffer.\n";
        return false;
    }

    render::TextureDesc depth_desc;
    depth_desc.width = 32U;
    depth_desc.height = 32U;
    depth_desc.format = render::TextureFormat::depth32_float;
    depth_desc.usage = render::TextureUsage::depth_attachment | render::TextureUsage::sampled;
    depth_desc.debug_name = "VICE supporting-pass shadow execution depth";
    auto depth = device->create_texture(depth_desc);

    render::TextureDesc motion_desc;
    motion_desc.width = 32U;
    motion_desc.height = 32U;
    motion_desc.format = render::TextureFormat::rgba16_float;
    motion_desc.usage = render::TextureUsage::color_attachment | render::TextureUsage::sampled;
    motion_desc.debug_name = "VICE supporting-pass motion execution color";
    auto motion = device->create_texture(motion_desc);
    if (!depth || !motion) {
        std::cerr << "Could not create supporting-pass render targets.\n";
        return false;
    }

    auto commands = device->create_command_list();
    if (!commands ||
        !visibility.prepare(*commands, 1U, &diagnostics) ||
        !current_models.upload(*commands, current, &diagnostics) ||
        !previous_models.upload(*commands, previous, &diagnostics) ||
        !batches.build(
            *commands,
            visibility.result(),
            assignments,
            draw_batches,
            &diagnostics
        )) {
        std::cerr << "Could not build supporting-pass indirect state: " << diagnostics << '\n';
        return false;
    }

    const auto batch_result = batches.result();
    const auto current_result = current_models.result();
    const auto previous_result = previous_models.result();
    if (batch_result.compacted_instance_indices == nullptr ||
        batch_result.indirect_arguments == nullptr ||
        current_result.models == nullptr || previous_result.models == nullptr) {
        std::cerr << "Supporting-pass GPU resources are unavailable.\n";
        return false;
    }

    const render::BufferViewDesc model_view{
        .offset = 0U,
        .size = sizeof(math::Mat4),
        .structure_stride = sizeof(math::Mat4),
    };
    const render::BufferViewDesc compacted_view{
        .offset = 0U,
        .size = sizeof(std::uint32_t),
        .structure_stride = sizeof(std::uint32_t),
    };

    if (!commands->begin_depth_render_pass(*depth, 1.0F, true) ||
        !commands->set_viewport(render::Viewport{.width = 32.0F, .height = 32.0F}) ||
        !commands->set_scissor_rect(render::ScissorRect{.width = 32U, .height = 32U}) ||
        !commands->set_graphics_pipeline(*shadow_pipeline) ||
        !commands->set_graphics_constant_buffer(frame_binding, *shadow_frame, 0U, sizeof(ShadowFrame)) ||
        !commands->set_graphics_buffer(current_models_binding, *current_result.models, model_view) ||
        !commands->set_graphics_buffer(
            shadow_compacted_binding,
            *batch_result.compacted_instance_indices,
            compacted_view
        ) ||
        !commands->set_index_buffer(*index_buffer, render::IndexFormat::uint32) ||
        !commands->draw_indexed_indirect(*batch_result.indirect_arguments) ||
        !commands->end_render_pass()) {
        std::cerr << "Could not execute indexed indirect shadow supporting pass.\n";
        return false;
    }

    render::RenderPassDesc motion_pass;
    motion_pass.clear_color = render::Color{0.0F, 0.0F, 0.0F, 0.0F};
    if (!commands->begin_render_pass(*motion, motion_pass) ||
        !commands->set_viewport(render::Viewport{.width = 32.0F, .height = 32.0F}) ||
        !commands->set_scissor_rect(render::ScissorRect{.width = 32U, .height = 32U}) ||
        !commands->set_graphics_pipeline(*motion_pipeline) ||
        !commands->set_graphics_constant_buffer(frame_binding, *motion_frame, 0U, sizeof(MotionFrame)) ||
        !commands->set_graphics_buffer(current_models_binding, *current_result.models, model_view) ||
        !commands->set_graphics_buffer(previous_models_binding, *previous_result.models, model_view) ||
        !commands->set_graphics_buffer(
            motion_compacted_binding,
            *batch_result.compacted_instance_indices,
            compacted_view
        ) ||
        !commands->set_index_buffer(*index_buffer, render::IndexFormat::uint32) ||
        !commands->draw_indexed_indirect(*batch_result.indirect_arguments) ||
        !commands->end_render_pass()) {
        std::cerr << "Could not execute indexed indirect motion supporting pass.\n";
        return false;
    }

    const auto submission = device->submit(std::move(commands));
    if (submission == render::invalid_submission || !device->wait_for_submission(submission)) {
        std::cerr << "Supporting-pass indexed indirect GPU execution failed.\n";
        return false;
    }

    std::cout << "GPU shadow + motion indexed indirect execution verified.\n";
    return true;
}

} // namespace

int main() {
    return run_supporting_pass_execution_smoke() ? 0 : 1;
}
