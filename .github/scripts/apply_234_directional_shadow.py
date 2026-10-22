from pathlib import Path

path = Path("engine/runtime/src/scene.cpp")
text = path.read_text()


def replace_once(old: str, new: str) -> None:
    global text
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"expected exactly one match, found {count}: {old[:120]!r}")
    text = text.replace(old, new, 1)


replace_once(
    '#include <vice/runtime/gpu_instance_table.hpp>\n#include <vice/runtime/post_process.hpp>',
    '#include <vice/runtime/gpu_instance_table.hpp>\n#include <vice/runtime/gpu_visibility_set.hpp>\n#include <vice/runtime/post_process.hpp>',
)
replace_once(
    '#include "gpu_pbr_batches.hpp"\n#include "material_pipeline.hpp"',
    '#include "gpu_pbr_batches.hpp"\n#include "gpu_shadow_batches.hpp"\n#include "material_pipeline.hpp"',
)

legacy_shadow_metal = '''constexpr std::string_view shadow_metal_source = R"(
#include <metal_stdlib>
using namespace metal;
struct ShadowFrame {
    float4x4 view_projection;
    float4x4 model;
};
struct VertexIn {
    float3 position [[attribute(11)]];
};
vertex float4 vice_runtime_shadow_vs(
    VertexIn input [[stage_in]],
    constant ShadowFrame& frame [[buffer(0)]]) {
    return frame.view_projection * frame.model * float4(input.position, 1.0);
}
)";
'''
gpu_shadow_sources = legacy_shadow_metal + '''
constexpr std::string_view gpu_shadow_hlsl_source = R"(
cbuffer ShadowFrame : register(b0) {
    float4x4 view_projection;
};
StructuredBuffer<float4x4> instance_models : register(t1);
StructuredBuffer<uint> compacted_instance_indices : register(t2);
struct VertexIn {
    float3 position : POSITION0;
};
float4 vice_runtime_gpu_shadow_vs(VertexIn input, uint instance_id : SV_InstanceID) : SV_Position {
    const uint scene_instance = compacted_instance_indices[instance_id];
    const float4 world_position = mul(
        instance_models[scene_instance],
        float4(input.position, 1.0)
    );
    return mul(view_projection, world_position);
}
)";

constexpr std::string_view gpu_shadow_metal_source = R"(
#include <metal_stdlib>
using namespace metal;
struct ShadowFrame {
    float4x4 view_projection;
};
struct VertexIn {
    float3 position [[attribute(11)]];
};
vertex float4 vice_runtime_gpu_shadow_vs(
    VertexIn input [[stage_in]],
    constant ShadowFrame& frame [[buffer(0)]],
    device const float4x4* instance_models [[buffer(1)]],
    device const uint* compacted_instance_indices [[buffer(2)]],
    uint instance_id [[instance_id]]) {
    const uint scene_instance = compacted_instance_indices[instance_id];
    return frame.view_projection * instance_models[scene_instance] * float4(input.position, 1.0);
}
)";
'''
replace_once(legacy_shadow_metal, gpu_shadow_sources)

frame_helper = '''[[nodiscard]] std::unique_ptr<render::Buffer> create_gpu_default_frame_buffer(
    render::Device& device
) {
    render::BufferDesc desc;
    desc.size = sizeof(GpuDefaultFrame);
    desc.usage = render::BufferUsage::uniform;
    desc.memory = render::MemoryLocation::cpu_to_gpu;
    desc.debug_name = "VICE Runtime GPU default frame constants";
    return device.create_buffer(desc);
}
'''
replace_once(
    frame_helper,
    frame_helper + '''
[[nodiscard]] std::unique_ptr<render::Buffer> create_gpu_shadow_frame_buffer(
    render::Device& device
) {
    render::BufferDesc desc;
    desc.size = sizeof(GpuDefaultFrame);
    desc.usage = render::BufferUsage::uniform;
    desc.memory = render::MemoryLocation::cpu_to_gpu;
    desc.debug_name = "VICE Runtime GPU directional shadow frame constants";
    return device.create_buffer(desc);
}
''',
)

replace_once(
    '''    RuntimeVisibilityCuller visibility;
    RuntimeGpuDrawBatcher gpu_draw_batches;
    RuntimeGpuDrawBatcher gpu_pbr_draw_batches;
    RuntimeGpuInstanceTable gpu_instances;''',
    '''    RuntimeVisibilityCuller visibility;
    RuntimeGpuIdentityVisibility gpu_shadow_visibility;
    RuntimeGpuDrawBatcher gpu_draw_batches;
    RuntimeGpuDrawBatcher gpu_pbr_draw_batches;
    RuntimeGpuDrawBatcher gpu_shadow_draw_batches;
    RuntimeGpuInstanceTable gpu_instances;''',
)
replace_once(
    '''    std::unique_ptr<render::Buffer> gpu_default_frame;
    std::unique_ptr<render::Buffer> gpu_pbr_frame;''',
    '''    std::unique_ptr<render::Buffer> gpu_default_frame;
    std::unique_ptr<render::Buffer> gpu_pbr_frame;
    std::array<std::unique_ptr<render::Buffer>, runtime_shadow_cascade_count> gpu_shadow_frames;''',
)
replace_once(
    '''    std::unique_ptr<render::ShaderModule> shadow_vertex_shader;
    std::unique_ptr<render::GraphicsPipeline> shadow_pipeline;''',
    '''    std::unique_ptr<render::ShaderModule> shadow_vertex_shader;
    std::unique_ptr<render::GraphicsPipeline> shadow_pipeline;
    std::unique_ptr<render::ShaderModule> gpu_shadow_vertex_shader;
    std::unique_ptr<render::GraphicsPipeline> gpu_shadow_pipeline;''',
)

replace_once(
    '''    if (!state_->visibility.initialize(device, diagnostics) ||
        !state_->gpu_draw_batches.initialize(device, diagnostics) ||
        !state_->gpu_pbr_draw_batches.initialize(device, diagnostics) ||
        !state_->gpu_instances.initialize(device, diagnostics)) {''',
    '''    if (!state_->visibility.initialize(device, diagnostics) ||
        !state_->gpu_shadow_visibility.initialize(device, diagnostics) ||
        !state_->gpu_draw_batches.initialize(device, diagnostics) ||
        !state_->gpu_pbr_draw_batches.initialize(device, diagnostics) ||
        !state_->gpu_shadow_draw_batches.initialize(device, diagnostics) ||
        !state_->gpu_instances.initialize(device, diagnostics)) {''',
)
replace_once(
    '''    state_->gpu_default_frame = create_gpu_default_frame_buffer(device);
    if (!state_->gpu_default_frame) {
        set_diagnostics(diagnostics, "VICE Runtime could not create GPU default frame constants.");
        return false;
    }
''',
    '''    state_->gpu_default_frame = create_gpu_default_frame_buffer(device);
    if (!state_->gpu_default_frame) {
        set_diagnostics(diagnostics, "VICE Runtime could not create GPU default frame constants.");
        return false;
    }
    for (auto& frame : state_->gpu_shadow_frames) {
        frame = create_gpu_shadow_frame_buffer(device);
        if (!frame) {
            set_diagnostics(diagnostics, "VICE Runtime could not create GPU directional shadow frame constants.");
            return false;
        }
    }
''',
)

legacy_pipeline = '''    state_->shadow_pipeline = device.create_graphics_pipeline(shadow_pipeline_desc);
    if (!state_->shadow_pipeline) {
        set_diagnostics(diagnostics, "VICE Runtime could not create the directional shadow pipeline.");
        return false;
    }
'''
replace_once(
    legacy_pipeline,
    legacy_pipeline + '''
    render::ShaderModuleDesc gpu_shadow_vertex_desc;
    gpu_shadow_vertex_desc.stage = render::ShaderStage::vertex;
    gpu_shadow_vertex_desc.language = vertex_desc.language;
    gpu_shadow_vertex_desc.source = metal ? gpu_shadow_metal_source : gpu_shadow_hlsl_source;
    gpu_shadow_vertex_desc.resources = vertex_resources;
    gpu_shadow_vertex_desc.entry_point = "vice_runtime_gpu_shadow_vs";
    gpu_shadow_vertex_desc.debug_name = "VICE Runtime GPU directional shadow vertex";
    state_->gpu_shadow_vertex_shader = device.create_shader_module(gpu_shadow_vertex_desc);
    if (!state_->gpu_shadow_vertex_shader) {
        set_diagnostics(diagnostics, "VICE Runtime could not create the GPU directional shadow shader.");
        return false;
    }

    render::GraphicsPipelineDesc gpu_shadow_pipeline_desc = shadow_pipeline_desc;
    gpu_shadow_pipeline_desc.vertex_shader = state_->gpu_shadow_vertex_shader.get();
    gpu_shadow_pipeline_desc.debug_name = "VICE Runtime GPU directional shadow pipeline";
    state_->gpu_shadow_pipeline = device.create_graphics_pipeline(gpu_shadow_pipeline_desc);
    if (!state_->gpu_shadow_pipeline) {
        set_diagnostics(diagnostics, "VICE Runtime could not create the GPU directional shadow pipeline.");
        return false;
    }
''',
)

replace_once(
    '''    const auto& gpu_pbr_plan = gpu_pbr_build.plan;

    RuntimeGpuDrawBatchResult gpu_default_batch_result{};
    RuntimeGpuDrawBatchResult gpu_pbr_batch_result{};
    if (!gpu_default_batches.empty() || !gpu_pbr_plan.batches.empty()) {
        if (!state_->gpu_instances.upload(*commands, gpu_instance_models, diagnostics)) {
            return false;
        }
    }
''',
    '''    const auto& gpu_pbr_plan = gpu_pbr_build.plan;
    const auto gpu_shadow_build = detail::build_runtime_gpu_shadow_batch_plan(
        instances,
        state_->catalog.records()
    );
    if (!gpu_shadow_build) {
        set_diagnostics(diagnostics, gpu_shadow_build.diagnostics);
        return false;
    }
    const auto& gpu_shadow_plan = gpu_shadow_build.plan;

    RuntimeGpuDrawBatchResult gpu_default_batch_result{};
    RuntimeGpuDrawBatchResult gpu_pbr_batch_result{};
    RuntimeGpuDrawBatchResult gpu_shadow_batch_result{};
    if (!instances.empty()) {
        if (!state_->gpu_instances.upload(*commands, gpu_instance_models, diagnostics)) {
            return false;
        }
    }
''',
)

replace_once(
    '''        gpu_pbr_batch_result = state_->gpu_pbr_draw_batches.result();
    }

    if (shadow_view.has_value()) {''',
    '''        gpu_pbr_batch_result = state_->gpu_pbr_draw_batches.result();
    }
    if (shadow_view.has_value() && !gpu_shadow_plan.batches.empty()) {
        if (!state_->gpu_shadow_visibility.prepare(
                *commands,
                static_cast<std::uint32_t>(instances.size()),
                diagnostics
            ) || !state_->gpu_shadow_draw_batches.build(
                *commands,
                state_->gpu_shadow_visibility.result(),
                gpu_shadow_plan.instance_batches,
                gpu_shadow_plan.batches,
                diagnostics
            )) {
            return false;
        }
        for (std::size_t cascade_index = 0U; cascade_index < runtime_shadow_cascade_count; ++cascade_index) {
            const GpuDefaultFrame shadow_frame{
                .view_projection = shadow_view->cascades[cascade_index].view_projection,
            };
            if (!state_->gpu_shadow_frames[cascade_index]->write(
                    0U, std::as_bytes(std::span{&shadow_frame, 1})
                )) {
                set_diagnostics(diagnostics, "VICE Runtime could not update GPU directional shadow frame constants.");
                return false;
            }
        }
        gpu_shadow_batch_result = state_->gpu_shadow_draw_batches.result();
    }

    if (shadow_view.has_value()) {''',
)

old_directional = '''    if (shadow_view.has_value()) {
        for (std::size_t cascade_index = 0; cascade_index < runtime_shadow_cascade_count; ++cascade_index) {
            if (!commands->begin_depth_render_pass(*state_->shadow_maps[cascade_index], 1.0F, true) ||
                !commands->set_viewport(render::Viewport{
                    .width = static_cast<float>(runtime_shadow_map_size),
                    .height = static_cast<float>(runtime_shadow_map_size),
                }) ||
                !commands->set_scissor_rect(render::ScissorRect{
                    .width = runtime_shadow_map_size,
                    .height = runtime_shadow_map_size,
                }) ||
                !commands->set_graphics_pipeline(*state_->shadow_pipeline)) {
                set_diagnostics(diagnostics, "VICE Runtime could not begin a directional shadow cascade.");
                return false;
            }

            auto& cascade_constants = state_->shadow_constants[cascade_index];
            for (const auto& instance : instances) {
                auto shadow_iterator = cascade_constants.find(instance.entity);
                if (shadow_iterator == cascade_constants.end()) {
                    auto constants = create_shadow_constant_buffer(*state_->device);
                    if (!constants) {
                        set_diagnostics(diagnostics, "VICE Runtime could not create shadow constants.");
                        return false;
                    }
                    shadow_iterator = cascade_constants.emplace(
                        instance.entity, std::move(constants)
                    ).first;
                }

                const ShadowConstants shadow_constants{
                    .view_projection = shadow_view->cascades[cascade_index].view_projection,
                    .model = runtime_model_matrix(instance),
                };
                if (!shadow_iterator->second->write(
                        0, std::as_bytes(std::span{&shadow_constants, 1})
                    )) {
                    set_diagnostics(diagnostics, "VICE Runtime could not update shadow constants.");
                    return false;
                }

                auto& mesh = state_->meshes.at(instance.mesh);
                if (!commands->set_vertex_buffer(0U, *mesh.vertices) ||
                    !commands->set_index_buffer(*mesh.indices, render::IndexFormat::uint32) ||
                    !commands->set_graphics_constant_buffer(
                        frame_binding,
                        *shadow_iterator->second,
                        0,
                        sizeof(ShadowConstants)
                    ) ||
                    !commands->draw_indexed(mesh.index_count)) {
                    set_diagnostics(
                        diagnostics,
                        "VICE Runtime could not encode directional shadow mesh `" +
                            instance.mesh.value.to_string() + "`."
                    );
                    return false;
                }
            }
            if (!commands->end_render_pass()) {
                set_diagnostics(diagnostics, "VICE Runtime could not finish a directional shadow cascade.");
                return false;
            }
        }
    }
'''
new_directional = '''    if (shadow_view.has_value()) {
        const auto instance_table = state_->gpu_instances.result();
        if (!gpu_shadow_plan.batches.empty() &&
            (instance_table.models == nullptr ||
             gpu_shadow_batch_result.compacted_instance_indices == nullptr ||
             gpu_shadow_batch_result.indirect_arguments == nullptr)) {
            set_diagnostics(diagnostics, "VICE Runtime GPU directional shadow batch resources are unavailable.");
            return false;
        }
        const render::BufferViewDesc model_view{
            .offset = 0U,
            .size = static_cast<std::uint64_t>(instances.size()) * sizeof(math::Mat4),
            .structure_stride = sizeof(math::Mat4),
        };
        const render::BufferViewDesc compacted_view{
            .offset = 0U,
            .size = static_cast<std::uint64_t>(instances.size()) * sizeof(std::uint32_t),
            .structure_stride = sizeof(std::uint32_t),
        };
        for (std::size_t cascade_index = 0; cascade_index < runtime_shadow_cascade_count; ++cascade_index) {
            if (!commands->begin_depth_render_pass(*state_->shadow_maps[cascade_index], 1.0F, true) ||
                !commands->set_viewport(render::Viewport{
                    .width = static_cast<float>(runtime_shadow_map_size),
                    .height = static_cast<float>(runtime_shadow_map_size),
                }) ||
                !commands->set_scissor_rect(render::ScissorRect{
                    .width = runtime_shadow_map_size,
                    .height = runtime_shadow_map_size,
                })) {
                set_diagnostics(diagnostics, "VICE Runtime could not begin a directional shadow cascade.");
                return false;
            }

            if (!gpu_shadow_plan.batches.empty()) {
                if (!commands->set_graphics_pipeline(*state_->gpu_shadow_pipeline) ||
                    !commands->set_graphics_constant_buffer(
                        frame_binding,
                        *state_->gpu_shadow_frames[cascade_index],
                        0U,
                        sizeof(GpuDefaultFrame)
                    ) ||
                    !commands->set_graphics_buffer(
                        gpu_model_table_binding, *instance_table.models, model_view
                    ) ||
                    !commands->set_graphics_buffer(
                        gpu_compacted_instance_binding,
                        *gpu_shadow_batch_result.compacted_instance_indices,
                        compacted_view
                    )) {
                    set_diagnostics(diagnostics, "VICE Runtime could not bind GPU directional shadow resources.");
                    return false;
                }
                for (std::size_t batch_index = 0U; batch_index < gpu_shadow_plan.batches.size(); ++batch_index) {
                    auto& mesh = state_->meshes.at(gpu_shadow_plan.batch_meshes[batch_index]);
                    const std::uint64_t argument_offset = static_cast<std::uint64_t>(batch_index) *
                        sizeof(render::IndexedIndirectDrawArguments);
                    if (!commands->set_vertex_buffer(0U, *mesh.vertices) ||
                        !commands->set_index_buffer(*mesh.indices, render::IndexFormat::uint32) ||
                        !commands->draw_indexed_indirect(
                            *gpu_shadow_batch_result.indirect_arguments,
                            argument_offset
                        )) {
                        set_diagnostics(
                            diagnostics,
                            "VICE Runtime could not encode a GPU-driven directional shadow batch."
                        );
                        return false;
                    }
                }
            }
            if (!commands->end_render_pass()) {
                set_diagnostics(diagnostics, "VICE Runtime could not finish a directional shadow cascade.");
                return false;
            }
        }
    }
'''
replace_once(old_directional, new_directional)

path.write_text(text)
print("Applied #234 GPU-driven directional shadow integration.")
