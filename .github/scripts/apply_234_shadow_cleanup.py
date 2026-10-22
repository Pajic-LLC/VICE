from pathlib import Path

path = Path("engine/runtime/src/scene.cpp")
text = path.read_text()


def replace_once(old: str, new: str) -> None:
    global text
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"expected exactly one match, found {count}: {old[:160]!r}")
    text = text.replace(old, new, 1)


replace_once(
'''struct ShadowConstants {
    math::Mat4 view_projection{};
    math::Mat4 model{};
};

''',
''
)
replace_once('static_assert(sizeof(ShadowConstants) == 128U);\n', '')

replace_once(
'''constexpr std::string_view shadow_hlsl_source = R"(
cbuffer ShadowFrame : register(b0) {
    float4x4 view_projection;
    float4x4 model;
};
struct VertexIn {
    float3 position : POSITION0;
};
float4 vice_runtime_shadow_vs(VertexIn input) : SV_Position {
    const float4 world_position = mul(model, float4(input.position, 1.0));
    return mul(view_projection, world_position);
}
)";

constexpr std::string_view shadow_metal_source = R"(
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

''',
''
)

replace_once(
'''[[nodiscard]] std::unique_ptr<render::Buffer> create_shadow_constant_buffer(
    render::Device& device
) {
    render::BufferDesc desc;
    desc.size = sizeof(ShadowConstants);
    desc.usage = render::BufferUsage::uniform;
    desc.memory = render::MemoryLocation::cpu_to_gpu;
    desc.debug_name = "VICE Runtime shadow constants";
    return device.create_buffer(desc);
}

''',
''
)

replace_once(
'''    std::unique_ptr<render::ShaderModule> shadow_vertex_shader;
    std::unique_ptr<render::GraphicsPipeline> shadow_pipeline;
    std::unique_ptr<render::ShaderModule> gpu_shadow_vertex_shader;''',
'''    std::unique_ptr<render::ShaderModule> gpu_shadow_vertex_shader;'''
)

replace_once(
'''    std::unordered_map<world::PersistentId, math::Mat4, world::PersistentIdHash> previous_models;
    std::array<
        std::unordered_map<world::PersistentId, std::unique_ptr<render::Buffer>, world::PersistentIdHash>,
        runtime_shadow_cascade_count
    > shadow_constants;
    std::array<
        std::array<
            std::unordered_map<world::PersistentId, std::unique_ptr<render::Buffer>, world::PersistentIdHash>,
            runtime_point_shadow_face_count
        >,
        runtime_point_shadow_light_count
    > point_shadow_constants;
    std::uint32_t depth_width = 0;''',
'''    std::unordered_map<world::PersistentId, math::Mat4, world::PersistentIdHash> previous_models;
    std::uint32_t depth_width = 0;'''
)

old_pipeline = '''    render::ShaderModuleDesc shadow_vertex_desc;
    shadow_vertex_desc.stage = render::ShaderStage::vertex;
    shadow_vertex_desc.language = vertex_desc.language;
    shadow_vertex_desc.source = metal ? shadow_metal_source : shadow_hlsl_source;
    shadow_vertex_desc.resources = vertex_resources;
    shadow_vertex_desc.entry_point = "vice_runtime_shadow_vs";
    shadow_vertex_desc.debug_name = "VICE Runtime directional shadow vertex";
    state_->shadow_vertex_shader = device.create_shader_module(shadow_vertex_desc);
    if (!state_->shadow_vertex_shader) {
        set_diagnostics(diagnostics, "VICE Runtime could not create the directional shadow shader.");
        return false;
    }

    render::GraphicsPipelineDesc shadow_pipeline_desc;
    shadow_pipeline_desc.vertex_shader = state_->shadow_vertex_shader.get();
    shadow_pipeline_desc.fragment_shader = nullptr;
    shadow_pipeline_desc.color_format = render::TextureFormat::undefined;
    shadow_pipeline_desc.cull_mode = render::CullMode::none;
    shadow_pipeline_desc.depth.format = render::TextureFormat::depth32_float;
    shadow_pipeline_desc.depth.test_enabled = true;
    shadow_pipeline_desc.depth.write_enabled = true;
    shadow_pipeline_desc.depth.compare = render::CompareOp::less;
    shadow_pipeline_desc.vertex_buffers = mesh_layouts;
    shadow_pipeline_desc.debug_name = "VICE Runtime directional shadow pipeline";
    state_->shadow_pipeline = device.create_graphics_pipeline(shadow_pipeline_desc);
    if (!state_->shadow_pipeline) {
        set_diagnostics(diagnostics, "VICE Runtime could not create the directional shadow pipeline.");
        return false;
    }

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
'''
new_pipeline = '''    render::ShaderModuleDesc gpu_shadow_vertex_desc;
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

    render::GraphicsPipelineDesc gpu_shadow_pipeline_desc;
    gpu_shadow_pipeline_desc.vertex_shader = state_->gpu_shadow_vertex_shader.get();
    gpu_shadow_pipeline_desc.fragment_shader = nullptr;
    gpu_shadow_pipeline_desc.color_format = render::TextureFormat::undefined;
    gpu_shadow_pipeline_desc.cull_mode = render::CullMode::none;
    gpu_shadow_pipeline_desc.depth.format = render::TextureFormat::depth32_float;
    gpu_shadow_pipeline_desc.depth.test_enabled = true;
    gpu_shadow_pipeline_desc.depth.write_enabled = true;
    gpu_shadow_pipeline_desc.depth.compare = render::CompareOp::less;
    gpu_shadow_pipeline_desc.vertex_buffers = mesh_layouts;
    gpu_shadow_pipeline_desc.debug_name = "VICE Runtime GPU directional shadow pipeline";
    state_->gpu_shadow_pipeline = device.create_graphics_pipeline(gpu_shadow_pipeline_desc);
'''
replace_once(old_pipeline, new_pipeline)

path.write_text(text)
print("Removed dead legacy per-draw shadow path from #234.")
