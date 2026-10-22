from pathlib import Path

path = Path("engine/runtime/src/scene.cpp")
text = path.read_text()


def replace_once(old: str, new: str) -> None:
    global text
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"expected exactly one match, found {count}: {old[:160]!r}")
    text = text.replace(old, new, 1)


vertex_resources = '''static constexpr std::array vertex_resources{
    render::ShaderResourceBindingDesc{
        .register_class = render::ShaderRegisterClass::constant_buffer,
        .binding = 0,
        .space = 0,
        .count = 1,
        .unbounded = false,
    },
    render::ShaderResourceBindingDesc{
        .register_class = render::ShaderRegisterClass::shader_resource,
        .binding = 1,
        .space = 0,
        .count = 1,
        .unbounded = false,
    },
    render::ShaderResourceBindingDesc{
        .register_class = render::ShaderRegisterClass::shader_resource,
        .binding = 2,
        .space = 0,
        .count = 1,
        .unbounded = false,
    },
};
'''
replace_once(
    vertex_resources,
    vertex_resources + '''
static constexpr std::array motion_vertex_resources{
    render::ShaderResourceBindingDesc{
        .register_class = render::ShaderRegisterClass::constant_buffer,
        .binding = 0,
        .space = 0,
        .count = 1,
        .unbounded = false,
    },
    render::ShaderResourceBindingDesc{
        .register_class = render::ShaderRegisterClass::shader_resource,
        .binding = 1,
        .space = 0,
        .count = 1,
        .unbounded = false,
    },
    render::ShaderResourceBindingDesc{
        .register_class = render::ShaderRegisterClass::shader_resource,
        .binding = 2,
        .space = 0,
        .count = 1,
        .unbounded = false,
    },
    render::ShaderResourceBindingDesc{
        .register_class = render::ShaderRegisterClass::shader_resource,
        .binding = 3,
        .space = 0,
        .count = 1,
        .unbounded = false,
    },
};
''',
)

replace_once(
    '''constexpr render::ShaderBindingKey gpu_compacted_instance_binding{
    .register_class = render::ShaderRegisterClass::shader_resource,
    .binding = 2,
    .space = 0,
    .array_element = 0,
};
''',
    '''constexpr render::ShaderBindingKey gpu_compacted_instance_binding{
    .register_class = render::ShaderRegisterClass::shader_resource,
    .binding = 2,
    .space = 0,
    .array_element = 0,
};

constexpr render::ShaderBindingKey gpu_motion_previous_model_binding{
    .register_class = render::ShaderRegisterClass::shader_resource,
    .binding = 2,
    .space = 0,
    .array_element = 0,
};

constexpr render::ShaderBindingKey gpu_motion_compacted_instance_binding{
    .register_class = render::ShaderRegisterClass::shader_resource,
    .binding = 3,
    .space = 0,
    .array_element = 0,
};
''',
)

replace_once(
    '''struct MotionConstants {
    math::Mat4 current_view_projection{};
    math::Mat4 previous_view_projection{};
    math::Mat4 current_model{};
    math::Mat4 previous_model{};
};''',
    '''struct MotionConstants {
    math::Mat4 current_view_projection{};
    math::Mat4 previous_view_projection{};
};''',
)
replace_once('static_assert(sizeof(MotionConstants) == 256U);', 'static_assert(sizeof(MotionConstants) == 128U);')

old_motion_sources = '''constexpr std::string_view motion_hlsl_source = R"(
cbuffer MotionFrame : register(b0) {
    float4x4 current_view_projection;
    float4x4 previous_view_projection;
    float4x4 current_model;
    float4x4 previous_model;
};
struct VertexIn {
    float3 position : POSITION0;
};
struct MotionVertexOut {
    float4 position : SV_Position;
    float4 current_clip : TEXCOORD0;
    float4 previous_clip : TEXCOORD1;
};
MotionVertexOut vice_runtime_motion_vs(VertexIn input) {
    MotionVertexOut output;
    const float4 current_world = mul(current_model, float4(input.position, 1.0));
    const float4 previous_world = mul(previous_model, float4(input.position, 1.0));
    output.current_clip = mul(current_view_projection, current_world);
    output.previous_clip = mul(previous_view_projection, previous_world);
    output.position = output.current_clip;
    return output;
}
float4 vice_runtime_motion_fs(MotionVertexOut input) : SV_Target0 {
    const float current_w = abs(input.current_clip.w) > 0.000001
        ? input.current_clip.w
        : (input.current_clip.w < 0.0 ? -0.000001 : 0.000001);
    const float previous_w = abs(input.previous_clip.w) > 0.000001
        ? input.previous_clip.w
        : (input.previous_clip.w < 0.0 ? -0.000001 : 0.000001);
    const float2 current_ndc = input.current_clip.xy / current_w;
    const float2 previous_ndc = input.previous_clip.xy / previous_w;
    const float2 velocity = (current_ndc - previous_ndc) * float2(0.5, -0.5);
    return float4(velocity, 0.0, 1.0);
}
)";

constexpr std::string_view motion_metal_source = R"(
#include <metal_stdlib>
using namespace metal;
struct MotionFrame {
    float4x4 current_view_projection;
    float4x4 previous_view_projection;
    float4x4 current_model;
    float4x4 previous_model;
};
struct VertexIn {
    float3 position [[attribute(11)]];
};
struct MotionVertexOut {
    float4 position [[position]];
    float4 current_clip;
    float4 previous_clip;
};
vertex MotionVertexOut vice_runtime_motion_vs(
    VertexIn input [[stage_in]],
    constant MotionFrame& frame [[buffer(0)]]) {
    MotionVertexOut output;
    const float4 current_world = frame.current_model * float4(input.position, 1.0);
    const float4 previous_world = frame.previous_model * float4(input.position, 1.0);
    output.current_clip = frame.current_view_projection * current_world;
    output.previous_clip = frame.previous_view_projection * previous_world;
    output.position = output.current_clip;
    return output;
}
fragment float4 vice_runtime_motion_fs(MotionVertexOut input [[stage_in]]) {
    const float current_w = abs(input.current_clip.w) > 0.000001
        ? input.current_clip.w
        : (input.current_clip.w < 0.0 ? -0.000001 : 0.000001);
    const float previous_w = abs(input.previous_clip.w) > 0.000001
        ? input.previous_clip.w
        : (input.previous_clip.w < 0.0 ? -0.000001 : 0.000001);
    const float2 current_ndc = input.current_clip.xy / current_w;
    const float2 previous_ndc = input.previous_clip.xy / previous_w;
    const float2 velocity = (current_ndc - previous_ndc) * float2(0.5, -0.5);
    return float4(velocity, 0.0, 1.0);
}
)";
'''
new_motion_sources = '''constexpr std::string_view motion_hlsl_source = R"(
cbuffer MotionFrame : register(b0) {
    float4x4 current_view_projection;
    float4x4 previous_view_projection;
};
StructuredBuffer<float4x4> current_models : register(t1);
StructuredBuffer<float4x4> previous_models : register(t2);
StructuredBuffer<uint> compacted_instance_indices : register(t3);
struct VertexIn {
    float3 position : POSITION0;
};
struct MotionVertexOut {
    float4 position : SV_Position;
    float4 current_clip : TEXCOORD0;
    float4 previous_clip : TEXCOORD1;
};
MotionVertexOut vice_runtime_motion_vs(VertexIn input, uint instance_id : SV_InstanceID) {
    MotionVertexOut output;
    const uint scene_instance = compacted_instance_indices[instance_id];
    const float4 current_world = mul(
        current_models[scene_instance], float4(input.position, 1.0)
    );
    const float4 previous_world = mul(
        previous_models[scene_instance], float4(input.position, 1.0)
    );
    output.current_clip = mul(current_view_projection, current_world);
    output.previous_clip = mul(previous_view_projection, previous_world);
    output.position = output.current_clip;
    return output;
}
float4 vice_runtime_motion_fs(MotionVertexOut input) : SV_Target0 {
    const float current_w = abs(input.current_clip.w) > 0.000001
        ? input.current_clip.w
        : (input.current_clip.w < 0.0 ? -0.000001 : 0.000001);
    const float previous_w = abs(input.previous_clip.w) > 0.000001
        ? input.previous_clip.w
        : (input.previous_clip.w < 0.0 ? -0.000001 : 0.000001);
    const float2 current_ndc = input.current_clip.xy / current_w;
    const float2 previous_ndc = input.previous_clip.xy / previous_w;
    const float2 velocity = (current_ndc - previous_ndc) * float2(0.5, -0.5);
    return float4(velocity, 0.0, 1.0);
}
)";

constexpr std::string_view motion_metal_source = R"(
#include <metal_stdlib>
using namespace metal;
struct MotionFrame {
    float4x4 current_view_projection;
    float4x4 previous_view_projection;
};
struct VertexIn {
    float3 position [[attribute(11)]];
};
struct MotionVertexOut {
    float4 position [[position]];
    float4 current_clip;
    float4 previous_clip;
};
vertex MotionVertexOut vice_runtime_motion_vs(
    VertexIn input [[stage_in]],
    constant MotionFrame& frame [[buffer(0)]],
    device const float4x4* current_models [[buffer(1)]],
    device const float4x4* previous_models [[buffer(2)]],
    device const uint* compacted_instance_indices [[buffer(3)]],
    uint instance_id [[instance_id]]) {
    MotionVertexOut output;
    const uint scene_instance = compacted_instance_indices[instance_id];
    const float4 current_world = current_models[scene_instance] * float4(input.position, 1.0);
    const float4 previous_world = previous_models[scene_instance] * float4(input.position, 1.0);
    output.current_clip = frame.current_view_projection * current_world;
    output.previous_clip = frame.previous_view_projection * previous_world;
    output.position = output.current_clip;
    return output;
}
fragment float4 vice_runtime_motion_fs(MotionVertexOut input [[stage_in]]) {
    const float current_w = abs(input.current_clip.w) > 0.000001
        ? input.current_clip.w
        : (input.current_clip.w < 0.0 ? -0.000001 : 0.000001);
    const float previous_w = abs(input.previous_clip.w) > 0.000001
        ? input.previous_clip.w
        : (input.previous_clip.w < 0.0 ? -0.000001 : 0.000001);
    const float2 current_ndc = input.current_clip.xy / current_w;
    const float2 previous_ndc = input.previous_clip.xy / previous_w;
    const float2 velocity = (current_ndc - previous_ndc) * float2(0.5, -0.5);
    return float4(velocity, 0.0, 1.0);
}
)";
'''
replace_once(old_motion_sources, new_motion_sources)

replace_once(
    '''    RuntimeGpuDrawBatcher gpu_pbr_draw_batches;
    RuntimeGpuDrawBatcher gpu_shadow_draw_batches;
    RuntimeGpuInstanceTable gpu_instances;''',
    '''    RuntimeGpuDrawBatcher gpu_pbr_draw_batches;
    RuntimeGpuDrawBatcher gpu_shadow_draw_batches;
    RuntimeGpuDrawBatcher gpu_motion_draw_batches;
    RuntimeGpuInstanceTable gpu_instances;
    RuntimeGpuInstanceTable gpu_previous_instances;''',
)
replace_once(
    '''    std::unique_ptr<render::ShaderModule> motion_vertex_shader;
    std::unique_ptr<render::ShaderModule> motion_fragment_shader;
    std::unique_ptr<render::GraphicsPipeline> motion_pipeline;''',
    '''    std::unique_ptr<render::ShaderModule> motion_vertex_shader;
    std::unique_ptr<render::ShaderModule> motion_fragment_shader;
    std::unique_ptr<render::GraphicsPipeline> motion_pipeline;
    std::unique_ptr<render::Buffer> gpu_motion_frame;''',
)
replace_once(
    '''    std::unordered_map<world::PersistentId, std::unique_ptr<render::Buffer>, world::PersistentIdHash> constants;
    std::unordered_map<world::PersistentId, std::unique_ptr<render::Buffer>, world::PersistentIdHash> motion_constants;
    std::unordered_map<world::PersistentId, math::Mat4, world::PersistentIdHash> previous_models;''',
    '''    std::unordered_map<world::PersistentId, std::unique_ptr<render::Buffer>, world::PersistentIdHash> constants;
    std::unordered_map<world::PersistentId, math::Mat4, world::PersistentIdHash> previous_models;''',
)

replace_once(
    '''        !state_->gpu_pbr_draw_batches.initialize(device, diagnostics) ||
        !state_->gpu_shadow_draw_batches.initialize(device, diagnostics) ||
        !state_->gpu_instances.initialize(device, diagnostics)) {''',
    '''        !state_->gpu_pbr_draw_batches.initialize(device, diagnostics) ||
        !state_->gpu_shadow_draw_batches.initialize(device, diagnostics) ||
        !state_->gpu_motion_draw_batches.initialize(device, diagnostics) ||
        !state_->gpu_instances.initialize(device, diagnostics) ||
        !state_->gpu_previous_instances.initialize(device, diagnostics)) {''',
)

replace_once(
    '''    motion_vertex_desc.source = metal ? motion_metal_source : motion_hlsl_source;
    motion_vertex_desc.resources = vertex_resources;''',
    '''    motion_vertex_desc.source = metal ? motion_metal_source : motion_hlsl_source;
    motion_vertex_desc.resources = motion_vertex_resources;''',
)
replace_once(
    '''    state_->motion_pipeline = device.create_graphics_pipeline(motion_pipeline_desc);
    if (!state_->motion_pipeline) {
        set_diagnostics(diagnostics, "VICE Runtime could not create the motion-vector pipeline.");
        return false;
    }
''',
    '''    state_->motion_pipeline = device.create_graphics_pipeline(motion_pipeline_desc);
    state_->gpu_motion_frame = create_motion_constant_buffer(device);
    if (!state_->motion_pipeline || !state_->gpu_motion_frame) {
        set_diagnostics(diagnostics, "VICE Runtime could not create the motion-vector pipeline/resources.");
        return false;
    }
''',
)

replace_once(
    '''    RuntimeGpuDrawBatchResult gpu_default_batch_result{};
    RuntimeGpuDrawBatchResult gpu_pbr_batch_result{};
    RuntimeGpuDrawBatchResult gpu_shadow_batch_result{};
    if (!instances.empty()) {
        if (!state_->gpu_instances.upload(*commands, gpu_instance_models, diagnostics)) {
            return false;
        }
    }
''',
    '''    RuntimeGpuDrawBatchResult gpu_default_batch_result{};
    RuntimeGpuDrawBatchResult gpu_pbr_batch_result{};
    RuntimeGpuDrawBatchResult gpu_shadow_batch_result{};
    RuntimeGpuDrawBatchResult gpu_motion_batch_result{};
    if (!instances.empty()) {
        if (!state_->gpu_instances.upload(*commands, gpu_instance_models, diagnostics)) {
            return false;
        }
    }
    if (!gpu_shadow_plan.batches.empty()) {
        std::vector<math::Mat4> gpu_previous_models;
        gpu_previous_models.reserve(instances.size());
        for (std::size_t instance_index = 0U; instance_index < instances.size(); ++instance_index) {
            math::Mat4 previous_model = gpu_instance_models[instance_index];
            if (temporal_frame.history_valid) {
                const auto previous = state_->previous_models.find(instances[instance_index].entity);
                if (previous != state_->previous_models.end()) previous_model = previous->second;
            }
            gpu_previous_models.push_back(previous_model);
        }
        if (!state_->gpu_previous_instances.upload(*commands, gpu_previous_models, diagnostics) ||
            !state_->gpu_motion_draw_batches.build(
                *commands,
                state_->visibility.result(),
                gpu_shadow_plan.instance_batches,
                gpu_shadow_plan.batches,
                diagnostics
            )) {
            return false;
        }
        const MotionConstants motion_frame{
            .current_view_projection = temporal_frame.current_view_projection,
            .previous_view_projection = temporal_frame.previous_view_projection,
        };
        if (!state_->gpu_motion_frame->write(
                0U, std::as_bytes(std::span{&motion_frame, 1})
            )) {
            set_diagnostics(diagnostics, "VICE Runtime could not update GPU motion-vector frame constants.");
            return false;
        }
        gpu_motion_batch_result = state_->gpu_motion_draw_batches.result();
    }
''',
)

old_motion_pass = '''    render::RenderPassDesc motion_pass;
    motion_pass.clear_color = render::Color{0.0F, 0.0F, 0.0F, 0.0F};
    motion_pass.depth_attachment = current_depth;
    motion_pass.clear_depth = 1.0F;
    motion_pass.clear_depth_attachment = false;
    if (!commands->begin_render_pass(*state_->motion_vectors, motion_pass) ||
        !commands->set_viewport(render::Viewport{
            .width = static_cast<float>(metrics.drawable_width),
            .height = static_cast<float>(metrics.drawable_height),
        }) ||
        !commands->set_scissor_rect(render::ScissorRect{
            .width = metrics.drawable_width,
            .height = metrics.drawable_height,
        }) ||
        !commands->set_graphics_pipeline(*state_->motion_pipeline)) {
        set_diagnostics(diagnostics, "VICE Runtime could not begin the motion-vector render pass.");
        return false;
    }

    for (const auto& instance : instances) {
        auto motion_iterator = state_->motion_constants.find(instance.entity);
        if (motion_iterator == state_->motion_constants.end()) {
            auto constants = create_motion_constant_buffer(*state_->device);
            if (!constants) {
                set_diagnostics(diagnostics, "VICE Runtime could not create motion-vector constants.");
                return false;
            }
            motion_iterator = state_->motion_constants.emplace(
                instance.entity,
                std::move(constants)
            ).first;
        }

        const math::Mat4 current_model = runtime_model_matrix(instance);
        math::Mat4 previous_model = current_model;
        if (temporal_frame.history_valid) {
            const auto previous = state_->previous_models.find(instance.entity);
            if (previous != state_->previous_models.end()) previous_model = previous->second;
        }
        const MotionConstants motion_constants{
            .current_view_projection = temporal_frame.current_view_projection,
            .previous_view_projection = temporal_frame.previous_view_projection,
            .current_model = current_model,
            .previous_model = previous_model,
        };
        if (!motion_iterator->second->write(
                0U,
                std::as_bytes(std::span{&motion_constants, 1})
            )) {
            set_diagnostics(diagnostics, "VICE Runtime could not update motion-vector constants.");
            return false;
        }

        auto& mesh = state_->meshes.at(instance.mesh);
        if (!commands->set_vertex_buffer(0U, *mesh.vertices) ||
            !commands->set_index_buffer(*mesh.indices, render::IndexFormat::uint32) ||
            !commands->set_graphics_constant_buffer(
                frame_binding,
                *motion_iterator->second,
                0U,
                sizeof(MotionConstants)
            ) ||
            !commands->draw_indexed(mesh.index_count)) {
            set_diagnostics(
                diagnostics,
                "VICE Runtime could not encode motion vectors for mesh `" +
                    instance.mesh.value.to_string() + "`."
            );
            return false;
        }
    }
    if (!commands->end_render_pass()) {
        set_diagnostics(diagnostics, "VICE Runtime could not finish the motion-vector render pass.");
        return false;
    }
'''
new_motion_pass = '''    render::RenderPassDesc motion_pass;
    motion_pass.clear_color = render::Color{0.0F, 0.0F, 0.0F, 0.0F};
    motion_pass.depth_attachment = current_depth;
    motion_pass.clear_depth = 1.0F;
    motion_pass.clear_depth_attachment = false;
    if (!commands->begin_render_pass(*state_->motion_vectors, motion_pass) ||
        !commands->set_viewport(render::Viewport{
            .width = static_cast<float>(metrics.drawable_width),
            .height = static_cast<float>(metrics.drawable_height),
        }) ||
        !commands->set_scissor_rect(render::ScissorRect{
            .width = metrics.drawable_width,
            .height = metrics.drawable_height,
        }) ||
        !commands->set_graphics_pipeline(*state_->motion_pipeline)) {
        set_diagnostics(diagnostics, "VICE Runtime could not begin the motion-vector render pass.");
        return false;
    }

    if (!gpu_shadow_plan.batches.empty()) {
        const auto current_instance_table = state_->gpu_instances.result();
        const auto previous_instance_table = state_->gpu_previous_instances.result();
        if (current_instance_table.models == nullptr ||
            previous_instance_table.models == nullptr ||
            gpu_motion_batch_result.compacted_instance_indices == nullptr ||
            gpu_motion_batch_result.indirect_arguments == nullptr) {
            set_diagnostics(diagnostics, "VICE Runtime GPU motion-vector batch resources are unavailable.");
            return false;
        }
        const render::BufferViewDesc motion_model_view{
            .offset = 0U,
            .size = static_cast<std::uint64_t>(instances.size()) * sizeof(math::Mat4),
            .structure_stride = sizeof(math::Mat4),
        };
        const render::BufferViewDesc motion_compacted_view{
            .offset = 0U,
            .size = static_cast<std::uint64_t>(instances.size()) * sizeof(std::uint32_t),
            .structure_stride = sizeof(std::uint32_t),
        };
        if (!commands->set_graphics_constant_buffer(
                frame_binding,
                *state_->gpu_motion_frame,
                0U,
                sizeof(MotionConstants)
            ) || !commands->set_graphics_buffer(
                gpu_model_table_binding,
                *current_instance_table.models,
                motion_model_view
            ) || !commands->set_graphics_buffer(
                gpu_motion_previous_model_binding,
                *previous_instance_table.models,
                motion_model_view
            ) || !commands->set_graphics_buffer(
                gpu_motion_compacted_instance_binding,
                *gpu_motion_batch_result.compacted_instance_indices,
                motion_compacted_view
            )) {
            set_diagnostics(diagnostics, "VICE Runtime could not bind GPU motion-vector resources.");
            return false;
        }
        for (std::size_t batch_index = 0U; batch_index < gpu_shadow_plan.batches.size(); ++batch_index) {
            auto& mesh = state_->meshes.at(gpu_shadow_plan.batch_meshes[batch_index]);
            const std::uint64_t argument_offset = static_cast<std::uint64_t>(batch_index) *
                sizeof(render::IndexedIndirectDrawArguments);
            if (!commands->set_vertex_buffer(0U, *mesh.vertices) ||
                !commands->set_index_buffer(*mesh.indices, render::IndexFormat::uint32) ||
                !commands->draw_indexed_indirect(
                    *gpu_motion_batch_result.indirect_arguments,
                    argument_offset
                )) {
                set_diagnostics(diagnostics, "VICE Runtime could not encode a GPU-driven motion-vector batch.");
                return false;
            }
        }
    }
    if (!commands->end_render_pass()) {
        set_diagnostics(diagnostics, "VICE Runtime could not finish the motion-vector render pass.");
        return false;
    }
'''
replace_once(old_motion_pass, new_motion_pass)

path.write_text(text)
print("Applied #234 GPU-driven motion-vector integration.")
