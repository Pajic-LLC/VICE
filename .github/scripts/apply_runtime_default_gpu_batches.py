from pathlib import Path


def replace_once(path: Path, old: str, new: str) -> None:
    text = path.read_text()
    if new in text:
        return
    if old not in text:
        raise SystemExit(f"missing patch context in {path}: {old[:120]!r}")
    path.write_text(text.replace(old, new, 1))


batcher = Path("engine/runtime/src/gpu_draw_batches.cpp")
replace_once(
    batcher,
    """    for (const std::uint32_t batch_index : instance_batches) {
        if (batch_index >= batch_count) {
            set_diagnostics(diagnostics, \"Runtime GPU draw batch assignment references an invalid batch.\");
            return false;
        }
""",
    """    for (const std::uint32_t batch_index : instance_batches) {
        if (batch_index == runtime_gpu_draw_batch_unassigned) continue;
        if (batch_index >= batch_count) {
            set_diagnostics(diagnostics, \"Runtime GPU draw batch assignment references an invalid batch.\");
            return false;
        }
""",
)

scene = Path("engine/runtime/src/scene.cpp")
replace_once(
    scene,
    """#include <vice/runtime/post_process.hpp>
#include <vice/runtime/visibility.hpp>
""",
    """#include <vice/runtime/gpu_draw_batches.hpp>
#include <vice/runtime/gpu_instance_table.hpp>
#include <vice/runtime/post_process.hpp>
#include <vice/runtime/visibility.hpp>
""",
)
replace_once(
    scene,
    """static constexpr std::array vertex_resources{
    render::ShaderResourceBindingDesc{
        .register_class = render::ShaderRegisterClass::constant_buffer,
        .binding = 0,
        .space = 0,
        .count = 1,
        .unbounded = false,
    },
};
""",
    """static constexpr std::array vertex_resources{
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
""",
)
replace_once(
    scene,
    """constexpr render::ShaderBindingKey frame_binding{
    .register_class = render::ShaderRegisterClass::constant_buffer,
    .binding = 0,
    .space = 0,
    .array_element = 0,
};
""",
    """constexpr render::ShaderBindingKey frame_binding{
    .register_class = render::ShaderRegisterClass::constant_buffer,
    .binding = 0,
    .space = 0,
    .array_element = 0,
};

constexpr render::ShaderBindingKey gpu_model_table_binding{
    .register_class = render::ShaderRegisterClass::shader_resource,
    .binding = 1,
    .space = 0,
    .array_element = 0,
};

constexpr render::ShaderBindingKey gpu_compacted_instance_binding{
    .register_class = render::ShaderRegisterClass::shader_resource,
    .binding = 2,
    .space = 0,
    .array_element = 0,
};
""",
)
replace_once(
    scene,
    """struct SceneConstants {
    math::Mat4 view_projection{};
""",
    """struct GpuDefaultFrame {
    math::Mat4 view_projection{};
};

struct SceneConstants {
    math::Mat4 view_projection{};
""",
)
replace_once(
    scene,
    """static_assert(sizeof(ToneMapConstants) == 16U);
""",
    """static_assert(sizeof(GpuDefaultFrame) == 64U);
static_assert(sizeof(ToneMapConstants) == 16U);
""",
)
replace_once(
    scene,
    '''constexpr std::string_view hlsl_source = R"(
cbuffer SceneFrame : register(b0) {
    float4x4 view_projection;
    float4x4 model;
};
struct VertexIn {
    float3 position : POSITION0;
    float3 color : COLOR0;
};
struct VertexOut {
    float4 position : SV_Position;
    float3 color : COLOR0;
};
VertexOut vice_runtime_mesh_vs(VertexIn input) {
    VertexOut output;
    float4 world_position = mul(model, float4(input.position, 1.0));
    output.position = mul(view_projection, world_position);
    output.color = input.color;
    return output;
}
float4 vice_runtime_mesh_fs(VertexOut input) : SV_Target0 {
    return float4(input.color, 1.0);
}
)";
''',
    '''constexpr std::string_view hlsl_source = R"(
cbuffer SceneFrame : register(b0) {
    float4x4 view_projection;
};
StructuredBuffer<float4x4> instance_models : register(t1);
StructuredBuffer<uint> compacted_instance_indices : register(t2);
struct VertexIn {
    float3 position : POSITION0;
    float3 color : COLOR0;
};
struct VertexOut {
    float4 position : SV_Position;
    float3 color : COLOR0;
};
VertexOut vice_runtime_mesh_vs(VertexIn input, uint instance_id : SV_InstanceID) {
    VertexOut output;
    const uint scene_instance = compacted_instance_indices[instance_id];
    const float4 world_position = mul(
        instance_models[scene_instance],
        float4(input.position, 1.0)
    );
    output.position = mul(view_projection, world_position);
    output.color = input.color;
    return output;
}
float4 vice_runtime_mesh_fs(VertexOut input) : SV_Target0 {
    return float4(input.color, 1.0);
}
)";
''',
)
replace_once(
    scene,
    '''constexpr std::string_view metal_source = R"(
#include <metal_stdlib>
using namespace metal;
struct SceneFrame {
    float4x4 view_projection;
    float4x4 model;
};
struct VertexIn {
    float3 position [[attribute(11)]];
    float3 color [[attribute(12)]];
};
struct VertexOut {
    float4 position [[position]];
    float3 color;
};
vertex VertexOut vice_runtime_mesh_vs(
    VertexIn input [[stage_in]],
    constant SceneFrame& frame [[buffer(0)]]) {
    VertexOut output;
    float4 world_position = frame.model * float4(input.position, 1.0);
    output.position = frame.view_projection * world_position;
    output.color = input.color;
    return output;
}
fragment float4 vice_runtime_mesh_fs(VertexOut input [[stage_in]]) {
    return float4(input.color, 1.0);
}
)";
''',
    '''constexpr std::string_view metal_source = R"(
#include <metal_stdlib>
using namespace metal;
struct SceneFrame {
    float4x4 view_projection;
};
struct VertexIn {
    float3 position [[attribute(11)]];
    float3 color [[attribute(12)]];
};
struct VertexOut {
    float4 position [[position]];
    float3 color;
};
vertex VertexOut vice_runtime_mesh_vs(
    VertexIn input [[stage_in]],
    constant SceneFrame& frame [[buffer(0)]],
    device const float4x4* instance_models [[buffer(1)]],
    device const uint* compacted_instance_indices [[buffer(2)]],
    uint instance_id [[instance_id]]) {
    VertexOut output;
    const uint scene_instance = compacted_instance_indices[instance_id];
    const float4 world_position = instance_models[scene_instance] * float4(input.position, 1.0);
    output.position = frame.view_projection * world_position;
    output.color = input.color;
    return output;
}
fragment float4 vice_runtime_mesh_fs(VertexOut input [[stage_in]]) {
    return float4(input.color, 1.0);
}
)";
''',
)
replace_once(
    scene,
    """[[nodiscard]] std::unique_ptr<render::Buffer> create_constant_buffer(render::Device& device) {
""",
    """[[nodiscard]] std::unique_ptr<render::Buffer> create_gpu_default_frame_buffer(
    render::Device& device
) {
    render::BufferDesc desc;
    desc.size = sizeof(GpuDefaultFrame);
    desc.usage = render::BufferUsage::uniform;
    desc.memory = render::MemoryLocation::cpu_to_gpu;
    desc.debug_name = \"VICE Runtime GPU default frame constants\";
    return device.create_buffer(desc);
}

[[nodiscard]] std::unique_ptr<render::Buffer> create_constant_buffer(render::Device& device) {
""",
)
replace_once(
    scene,
    """    detail::RuntimeEnvironment environment;
    RuntimeVisibilityCuller visibility;
    std::unique_ptr<render::ShaderModule> vertex_shader;
""",
    """    detail::RuntimeEnvironment environment;
    RuntimeVisibilityCuller visibility;
    RuntimeGpuDrawBatcher gpu_draw_batches;
    RuntimeGpuInstanceTable gpu_instances;
    std::unique_ptr<render::Buffer> gpu_default_frame;
    std::unique_ptr<render::ShaderModule> vertex_shader;
""",
)
replace_once(
    scene,
    """    if (!state_->visibility.initialize(device, diagnostics)) {
        return false;
    }

    const bool metal""",
    """    if (!state_->visibility.initialize(device, diagnostics) ||
        !state_->gpu_draw_batches.initialize(device, diagnostics) ||
        !state_->gpu_instances.initialize(device, diagnostics)) {
        return false;
    }
    state_->gpu_default_frame = create_gpu_default_frame_buffer(device);
    if (!state_->gpu_default_frame) {
        set_diagnostics(diagnostics, \"VICE Runtime could not create GPU default frame constants.\");
        return false;
    }

    const bool metal""",
)

visibility_anchor = """    if (!state_->visibility.cull(
            *commands,
            visibility_spheres,
            visibility_frustum,
            diagnostics
        )) {
        return false;
    }
"""
gpu_setup = visibility_anchor + """
    std::vector<math::Mat4> gpu_instance_models;
    gpu_instance_models.reserve(instances.size());
    std::vector<std::uint32_t> gpu_batch_assignments(
        instances.size(), runtime_gpu_draw_batch_unassigned
    );
    std::vector<RuntimeGpuDrawBatch> gpu_default_batches;
    std::vector<world::ResourceId> gpu_default_batch_meshes;
    std::unordered_map<world::ResourceId, std::uint32_t, world::ResourceIdHash> gpu_default_batch_lookup;
    for (std::size_t instance_index = 0U; instance_index < instances.size(); ++instance_index) {
        const auto& instance = instances[instance_index];
        gpu_instance_models.push_back(runtime_model_matrix(instance));
        if (instance.material.has_value()) continue;

        auto found = gpu_default_batch_lookup.find(instance.mesh);
        if (found == gpu_default_batch_lookup.end()) {
            const std::uint32_t batch_index = static_cast<std::uint32_t>(gpu_default_batches.size());
            const auto& mesh = state_->meshes.at(instance.mesh);
            gpu_default_batches.push_back(RuntimeGpuDrawBatch{
                .index_count = mesh.index_count,
                .first_index = 0U,
                .vertex_offset = 0,
            });
            gpu_default_batch_meshes.push_back(instance.mesh);
            found = gpu_default_batch_lookup.emplace(instance.mesh, batch_index).first;
        }
        gpu_batch_assignments[instance_index] = found->second;
    }

    RuntimeGpuDrawBatchResult gpu_default_batch_result{};
    if (!gpu_default_batches.empty()) {
        if (!state_->gpu_instances.upload(*commands, gpu_instance_models, diagnostics) ||
            !state_->gpu_draw_batches.build(
                *commands,
                state_->visibility.result(),
                gpu_batch_assignments,
                gpu_default_batches,
                diagnostics
            )) {
            return false;
        }
        const GpuDefaultFrame gpu_frame{.view_projection = view_projection};
        if (!state_->gpu_default_frame->write(
                0U, std::as_bytes(std::span{&gpu_frame, 1})
            )) {
            set_diagnostics(diagnostics, \"VICE Runtime could not update GPU default frame constants.\");
            return false;
        }
        gpu_default_batch_result = state_->gpu_draw_batches.result();
    }
"""
text = scene.read_text()
if gpu_setup not in text:
    if visibility_anchor not in text:
        raise SystemExit("missing visibility integration anchor")
    scene.write_text(text.replace(visibility_anchor, gpu_setup, 1))

scene_loop_anchor = """    for (const auto& instance : instances) {
        auto constants_iterator = state_->constants.find(instance.entity);
"""
gpu_draw_block = """    if (!gpu_default_batches.empty()) {
        const auto instance_table = state_->gpu_instances.result();
        if (instance_table.models == nullptr || gpu_default_batch_result.compacted_instance_indices == nullptr ||
            gpu_default_batch_result.indirect_arguments == nullptr) {
            set_diagnostics(diagnostics, \"VICE Runtime GPU default batch resources are unavailable.\");
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
        if (!commands->set_graphics_pipeline(*state_->pipeline) ||
            !commands->set_graphics_constant_buffer(
                frame_binding, *state_->gpu_default_frame, 0U, sizeof(GpuDefaultFrame)
            ) ||
            !commands->set_graphics_buffer(
                gpu_model_table_binding, *instance_table.models, model_view
            ) ||
            !commands->set_graphics_buffer(
                gpu_compacted_instance_binding,
                *gpu_default_batch_result.compacted_instance_indices,
                compacted_view
            )) {
            set_diagnostics(diagnostics, \"VICE Runtime could not bind GPU default scene resources.\");
            return false;
        }
        for (std::size_t batch_index = 0U; batch_index < gpu_default_batches.size(); ++batch_index) {
            auto& mesh = state_->meshes.at(gpu_default_batch_meshes[batch_index]);
            const std::uint64_t argument_offset = static_cast<std::uint64_t>(batch_index) *
                sizeof(render::IndexedIndirectDrawArguments);
            if (!commands->set_vertex_buffer(0U, *mesh.vertices) ||
                !commands->set_index_buffer(*mesh.indices, render::IndexFormat::uint32) ||
                !commands->draw_indexed_indirect(
                    *gpu_default_batch_result.indirect_arguments,
                    argument_offset
                )) {
                set_diagnostics(
                    diagnostics,
                    \"VICE Runtime could not encode a GPU-driven default mesh batch.\"
                );
                return false;
            }
        }
    }

    for (const auto& instance : instances) {
        if (!instance.material.has_value()) continue;
        auto constants_iterator = state_->constants.find(instance.entity);
"""
text = scene.read_text()
if gpu_draw_block not in text:
    if scene_loop_anchor not in text:
        raise SystemExit("missing HDR scene object-loop anchor")
    scene.write_text(text.replace(scene_loop_anchor, gpu_draw_block, 1))
