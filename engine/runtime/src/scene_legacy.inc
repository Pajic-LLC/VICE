#include <vice/runtime/scene.hpp>
#include <vice/runtime/directional_shadow.hpp>

#include <vice/render/shader_asset.hpp>
#include <vice/runtime/gpu_cluster_instance_remap.hpp>
#include <vice/runtime/gpu_cluster_visibility.hpp>
#include <vice/runtime/gpu_draw_batches.hpp>
#include <vice/runtime/gpu_instance_table.hpp>
#include <vice/runtime/gpu_meshlet_table.hpp>
#include <vice/runtime/gpu_visibility_set.hpp>
#include <vice/runtime/hiz.hpp>
#include <vice/runtime/hiz_occlusion.hpp>
#include <vice/runtime/post_process.hpp>
#include <vice/runtime/ray_tracing_scene.hpp>
#include <vice/runtime/ray_tracing_scene_executor.hpp>
#include <vice/runtime/render_streaming.hpp>
#include <vice/runtime/resource_residency.hpp>
#include <vice/runtime/visibility.hpp>

#include "bloom_shaders.hpp"
#include "environment_pipeline.hpp"
#include "gpu_material_scene.hpp"
#include "gpu_motion_history.hpp"
#include "gpu_pbr_batches.hpp"
#include "gpu_shadow_batches.hpp"
#include "hiz_history.hpp"
#include "material_pipeline.hpp"
#include "sky_shaders.hpp"
#include "taa_shaders.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace vice::runtime {
namespace {

constexpr float runtime_fov = 0.82F;

static constexpr std::array mesh_attributes{
    render::VertexAttributeDesc{
        .semantic = "POSITION",
        .semantic_index = 0,
        .shader_location = 0,
        .format = render::VertexFormat::float32x3,
        .offset = 0,
    },
    render::VertexAttributeDesc{
        .semantic = "COLOR",
        .semantic_index = 0,
        .shader_location = 1,
        .format = render::VertexFormat::float32x3,
        .offset = 12,
    },
    render::VertexAttributeDesc{
        .semantic = "NORMAL",
        .semantic_index = 0,
        .shader_location = 2,
        .format = render::VertexFormat::float32x3,
        .offset = 24,
    },
    render::VertexAttributeDesc{
        .semantic = "TEXCOORD",
        .semantic_index = 0,
        .shader_location = 3,
        .format = render::VertexFormat::float32x2,
        .offset = 36,
    },
};

static constexpr std::array mesh_layouts{
    render::VertexBufferLayoutDesc{
        .stride = sizeof(render::MeshVertex),
        .step_mode = render::VertexStepMode::per_vertex,
        .step_rate = 1,
        .attributes = mesh_attributes,
    },
};

static constexpr std::array vertex_resources{
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

constexpr render::ShaderBindingKey frame_binding{
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

constexpr render::ShaderBindingKey gpu_pbr_model_table_binding{
    .register_class = render::ShaderRegisterClass::shader_resource,
    .binding = 0,
    .space = 0,
    .array_element = 0,
};

constexpr render::ShaderBindingKey gpu_pbr_compacted_instance_binding{
    .register_class = render::ShaderRegisterClass::shader_resource,
    .binding = 1,
    .space = 0,
    .array_element = 0,
};

constexpr render::ShaderBindingKey gpu_pbr_instance_material_binding{
    .register_class = render::ShaderRegisterClass::shader_resource,
    .binding = 2,
    .space = 0,
    .array_element = 0,
};

constexpr render::ShaderBindingKey gpu_pbr_material_record_binding{
    .register_class = render::ShaderRegisterClass::shader_resource,
    .binding = 3,
    .space = 0,
    .array_element = 0,
};

[[nodiscard]] constexpr render::ShaderBindingKey gpu_pbr_material_texture_binding(
    const std::uint32_t array_element
) noexcept {
    return render::ShaderBindingKey{
        .register_class = render::ShaderRegisterClass::shader_resource,
        .binding = 0,
        .space = 1,
        .array_element = array_element,
    };
}

[[nodiscard]] constexpr render::ShaderBindingKey gpu_pbr_material_sampler_binding(
    const std::uint32_t array_element
) noexcept {
    return render::ShaderBindingKey{
        .register_class = render::ShaderRegisterClass::sampler,
        .binding = 0,
        .space = 1,
        .array_element = array_element,
    };
}

[[nodiscard]] constexpr render::ShaderBindingKey gpu_pbr_lighting_texture_binding(
    const std::uint32_t binding
) noexcept {
    return render::ShaderBindingKey{
        .register_class = render::ShaderRegisterClass::shader_resource,
        .binding = binding,
        .space = 2,
        .array_element = 0,
    };
}

[[nodiscard]] constexpr render::ShaderBindingKey gpu_pbr_lighting_sampler_binding(
    const std::uint32_t binding
) noexcept {
    return render::ShaderBindingKey{
        .register_class = render::ShaderRegisterClass::sampler,
        .binding = binding,
        .space = 2,
        .array_element = 0,
    };
}

constexpr render::ShaderBindingKey material_binding{
    .register_class = render::ShaderRegisterClass::constant_buffer,
    .binding = 1,
    .space = 0,
    .array_element = 0,
};

constexpr render::ShaderBindingKey base_color_texture_binding{
    .register_class = render::ShaderRegisterClass::shader_resource,
    .binding = 0,
    .space = 0,
    .array_element = 0,
};

constexpr render::ShaderBindingKey normal_texture_binding{
    .register_class = render::ShaderRegisterClass::shader_resource,
    .binding = 1,
    .space = 0,
    .array_element = 0,
};

constexpr render::ShaderBindingKey orm_texture_binding{
    .register_class = render::ShaderRegisterClass::shader_resource,
    .binding = 2,
    .space = 0,
    .array_element = 0,
};

constexpr render::ShaderBindingKey emissive_texture_binding{
    .register_class = render::ShaderRegisterClass::shader_resource,
    .binding = 3,
    .space = 0,
    .array_element = 0,
};

constexpr render::ShaderBindingKey material_sampler_binding{
    .register_class = render::ShaderRegisterClass::sampler,
    .binding = 0,
    .space = 0,
    .array_element = 0,
};

[[nodiscard]] constexpr render::ShaderBindingKey shadow_texture_binding(
    const std::uint32_t base,
    const std::size_t cascade_index
) noexcept {
    return render::ShaderBindingKey{
        .register_class = render::ShaderRegisterClass::shader_resource,
        .binding = base + static_cast<std::uint32_t>(cascade_index),
        .space = 0,
        .array_element = 0,
    };
}

[[nodiscard]] constexpr render::ShaderBindingKey point_shadow_texture_binding(
    const std::uint32_t base,
    const std::size_t light_slot,
    const std::size_t face_index
) noexcept {
    return render::ShaderBindingKey{
        .register_class = render::ShaderRegisterClass::shader_resource,
        .binding = base + static_cast<std::uint32_t>(
            light_slot * runtime_point_shadow_face_count + face_index
        ),
        .space = 0,
        .array_element = 0,
    };
}

constexpr render::ShaderBindingKey shadow_sampler_binding{
    .register_class = render::ShaderRegisterClass::sampler,
    .binding = 1,
    .space = 0,
    .array_element = 0,
};

[[nodiscard]] constexpr render::ShaderBindingKey environment_texture_binding(
    const std::size_t source_index,
    const std::uint32_t component
) noexcept {
    return render::ShaderBindingKey{
        .register_class = render::ShaderRegisterClass::shader_resource,
        .binding = 8U + static_cast<std::uint32_t>(source_index) * 3U + component,
        .space = 0,
        .array_element = 0,
    };
}

constexpr render::ShaderBindingKey environment_sampler_binding{
    .register_class = render::ShaderRegisterClass::sampler,
    .binding = 2,
    .space = 0,
    .array_element = 0,
};

constexpr render::ShaderBindingKey tone_map_constants_binding{
    .register_class = render::ShaderRegisterClass::constant_buffer,
    .binding = 0,
    .space = 0,
    .array_element = 0,
};

constexpr render::ShaderBindingKey tone_map_scene_texture_binding{
    .register_class = render::ShaderRegisterClass::shader_resource,
    .binding = 0,
    .space = 0,
    .array_element = 0,
};

constexpr render::ShaderBindingKey tone_map_sampler_binding{
    .register_class = render::ShaderRegisterClass::sampler,
    .binding = 0,
    .space = 0,
    .array_element = 0,
};

static constexpr std::array tone_map_fragment_resources{
    render::ShaderResourceBindingDesc{
        .register_class = render::ShaderRegisterClass::constant_buffer,
        .binding = 0,
        .space = 0,
        .count = 1,
        .unbounded = false,
    },
    render::ShaderResourceBindingDesc{
        .register_class = render::ShaderRegisterClass::shader_resource,
        .binding = 0,
        .space = 0,
        .count = 1,
        .unbounded = false,
    },
    render::ShaderResourceBindingDesc{
        .register_class = render::ShaderRegisterClass::sampler,
        .binding = 0,
        .space = 0,
        .count = 1,
        .unbounded = false,
    },
};

struct ToneMapConstants {
    float exposure = 1.0F;
    float vignette_intensity = 0.12F;
    float vignette_inner = 0.35F;
    float vignette_outer = 1.50F;
};

struct BloomConstants {
    float texel_size[2]{1.0F, 1.0F};
    float threshold = 1.0F;
    float soft_knee = 0.5F;
    float strength = 0.08F;
    float direction[2]{};
    float padding = 0.0F;
};

struct SceneLightConstants {
    float position_or_direction[4]{};
    float color_intensity[4]{};
    float parameters[4]{};
};

struct SceneReflectionProjectionConstants {
    math::Mat4 probe_from_model{};
    float half_extents_enabled[4]{};
    float rotation[4]{};
};

struct ScenePointShadowConstants {
    float metadata[4]{};
    math::Mat4 model_to_light{};
    std::array<math::Mat4, runtime_point_shadow_face_count> face_view_projections{};
};

struct GpuDefaultFrame {
    math::Mat4 view_projection{};
};

struct SceneConstants {
    math::Mat4 view_projection{};
    math::Mat4 model{};
    float camera_position[4]{};
    float light_metadata[4]{};
    std::array<SceneLightConstants, max_runtime_lights> lights{};
    math::Mat4 shadow_view_projection{};
    float shadow_metadata[4]{};
    std::array<math::Mat4, runtime_shadow_cascade_count - 1U> shadow_cascade_view_projections{};
    float shadow_cascade_splits[4]{};
    float shadow_camera_forward[4]{};
    float environment_metadata[4]{};
    float environment_secondary_metadata[4]{};
    float environment_tertiary_metadata[4]{};
    std::array<SceneReflectionProjectionConstants, 3U> environment_projections{};
    math::Mat4 atmosphere_camera_from_model{};
    float atmosphere_sun_direction_intensity[4]{};
    float atmosphere_sun_color[4]{};
    float atmosphere_parameters[4]{};
    float atmosphere_secondary[4]{};
    float atmosphere_ground_albedo[4]{};
    std::array<ScenePointShadowConstants, runtime_point_shadow_light_count> point_shadows{};
    float ambient_occlusion_metadata[4]{};
};

struct GpuPbrSceneConstants {
    math::Mat4 view_projection{};
    float camera_position[4]{};
    float light_metadata[4]{};
    std::array<SceneLightConstants, max_runtime_lights> lights{};
    math::Mat4 shadow_view_projection{};
    float shadow_metadata[4]{};
    std::array<math::Mat4, runtime_shadow_cascade_count - 1U> shadow_cascade_view_projections{};
    float shadow_cascade_splits[4]{};
    float shadow_camera_forward[4]{};
    float environment_metadata[4]{};
    float environment_secondary_metadata[4]{};
    float environment_tertiary_metadata[4]{};
    std::array<SceneReflectionProjectionConstants, 3U> environment_projections{};
    math::Mat4 atmosphere_camera_from_model{};
    float atmosphere_sun_direction_intensity[4]{};
    float atmosphere_sun_color[4]{};
    float atmosphere_parameters[4]{};
    float atmosphere_secondary[4]{};
    float atmosphere_ground_albedo[4]{};
    std::array<ScenePointShadowConstants, runtime_point_shadow_light_count> point_shadows{};
    float ambient_occlusion_metadata[4]{};
};

struct MotionConstants {
    math::Mat4 current_view_projection{};
    math::Mat4 previous_view_projection{};
};

struct TaaConstants {
    float texel_size[2]{};
    float history_weight = 0.9F;
    float depth_threshold = 0.0025F;
    float history_valid = 0.0F;
    float motion_rejection = 32.0F;
    float padding[2]{};
};

static_assert(sizeof(GpuDefaultFrame) == 64U);
static_assert(sizeof(ToneMapConstants) == 16U);
static_assert(sizeof(BloomConstants) == 32U);
static_assert(sizeof(SceneLightConstants) == 48U);
static_assert(sizeof(SceneReflectionProjectionConstants) == 96U);
static_assert(sizeof(ScenePointShadowConstants) == 464U);
static_assert(sizeof(SceneConstants) == 2272U);
static_assert(sizeof(GpuPbrSceneConstants) == 2208U);
static_assert(sizeof(MotionConstants) == 128U);
static_assert(sizeof(TaaConstants) == 32U);

constexpr std::string_view hlsl_source = R"(
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

constexpr std::string_view metal_source = R"(
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

constexpr std::string_view motion_hlsl_source = R"(
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

constexpr std::string_view tone_map_hlsl_source = R"(
cbuffer ToneMapFrame : register(b0) {
    float4 tone_map_data;
};
Texture2D<float4> scene_color : register(t0);
SamplerState scene_sampler : register(s0);
struct ToneMapVertexOut {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};
ToneMapVertexOut vice_runtime_tonemap_vs(uint vertex_id : SV_VertexID) {
    const float2 uvs[3] = {
        float2(0.0, 0.0),
        float2(2.0, 0.0),
        float2(0.0, 2.0)
    };
    ToneMapVertexOut output;
    output.uv = uvs[vertex_id];
    output.position = float4(
        output.uv.x * 2.0 - 1.0,
        1.0 - output.uv.y * 2.0,
        0.0,
        1.0
    );
    return output;
}
float3 vice_aces_fitted(float3 color) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return saturate((color * (a * color + b)) / (color * (c * color + d) + e));
}
float vice_linear_to_srgb_channel(float value) {
    value = saturate(value);
    return value <= 0.0031308
        ? value * 12.92
        : 1.055 * pow(value, 1.0 / 2.4) - 0.055;
}
float4 vice_runtime_tonemap_fs(ToneMapVertexOut input) : SV_Target0 {
    float3 hdr = max(scene_color.Sample(scene_sampler, input.uv).rgb * tone_map_data.x, 0.0);
    const float2 centered = input.uv * 2.0 - 1.0;
    const float radius_squared = dot(centered, centered);
    const float vignette = 1.0 - saturate(tone_map_data.y) *
        smoothstep(tone_map_data.z, tone_map_data.w, radius_squared);
    hdr *= vignette;
    const float3 mapped = vice_aces_fitted(hdr);
    return float4(
        vice_linear_to_srgb_channel(mapped.r),
        vice_linear_to_srgb_channel(mapped.g),
        vice_linear_to_srgb_channel(mapped.b),
        1.0
    );
}
)";

constexpr std::string_view tone_map_metal_source = R"(
#include <metal_stdlib>
using namespace metal;
struct ToneMapFrame {
    float4 tone_map_data;
};
struct ToneMapVertexOut {
    float4 position [[position]];
    float2 uv;
};
vertex ToneMapVertexOut vice_runtime_tonemap_vs(uint vertex_id [[vertex_id]]) {
    constexpr float2 uvs[3] = {
        float2(0.0, 0.0),
        float2(2.0, 0.0),
        float2(0.0, 2.0)
    };
    ToneMapVertexOut output;
    output.uv = uvs[vertex_id];
    output.position = float4(
        output.uv.x * 2.0 - 1.0,
        1.0 - output.uv.y * 2.0,
        0.0,
        1.0
    );
    return output;
}
float3 vice_aces_fitted(float3 color) {
    constexpr float a = 2.51;
    constexpr float b = 0.03;
    constexpr float c = 2.43;
    constexpr float d = 0.59;
    constexpr float e = 0.14;
    return clamp((color * (a * color + b)) / (color * (c * color + d) + e), 0.0, 1.0);
}
float vice_linear_to_srgb_channel(float value) {
    value = clamp(value, 0.0, 1.0);
    return value <= 0.0031308
        ? value * 12.92
        : 1.055 * pow(value, 1.0 / 2.4) - 0.055;
}
fragment float4 vice_runtime_tonemap_fs(
    ToneMapVertexOut input [[stage_in]],
    constant ToneMapFrame& frame [[buffer(0)]],
    texture2d<float> scene_color [[texture(0)]],
    sampler scene_sampler [[sampler(0)]]) {
    float3 hdr = max(scene_color.sample(scene_sampler, input.uv).rgb * frame.tone_map_data.x, 0.0);
    const float2 centered = input.uv * 2.0 - 1.0;
    const float radius_squared = dot(centered, centered);
    const float vignette = 1.0 - clamp(frame.tone_map_data.y, 0.0, 1.0) *
        smoothstep(frame.tone_map_data.z, frame.tone_map_data.w, radius_squared);
    hdr *= vignette;
    const float3 mapped = vice_aces_fitted(hdr);
    return float4(
        vice_linear_to_srgb_channel(mapped.r),
        vice_linear_to_srgb_channel(mapped.g),
        vice_linear_to_srgb_channel(mapped.b),
        1.0
    );
}
)";

[[nodiscard]] constexpr math::Vec3 add(
    const math::Vec3 left,
    const math::Vec3 right
) noexcept {
    return {left.x + right.x, left.y + right.y, left.z + right.z};
}

[[nodiscard]] constexpr math::Vec3 scaled(
    const math::Vec3 value,
    const float scale
) noexcept {
    return {value.x * scale, value.y * scale, value.z * scale};
}

[[nodiscard]] math::Vec3 rotate_vector(
    const world::AuthoredRotation rotation,
    const math::Vec3 value
) noexcept {
    const math::Vec3 axis{rotation.x, rotation.y, rotation.z};
    const float axis_dot = math::dot(axis, value);
    const float axis_length_squared = math::dot(axis, axis);
    return add(
        add(
            scaled(axis, 2.0F * axis_dot),
            scaled(value, rotation.w * rotation.w - axis_length_squared)
        ),
        scaled(math::cross(axis, value), 2.0F * rotation.w)
    );
}

[[nodiscard]] math::Mat4 scale_matrix(const world::AuthoredScale scale) noexcept {
    math::Mat4 result = math::identity();
    result(0, 0) = scale.x;
    result(1, 1) = scale.y;
    result(2, 2) = scale.z;
    return result;
}

[[nodiscard]] math::Mat4 reflection_probe_from_model(
    const RenderMeshInstance& instance,
    const RuntimeReflectionProbe& probe
) noexcept {
    const math::Vec3 relative_origin{
        static_cast<float>(instance.spatial.global_position.x - probe.position.x),
        static_cast<float>(instance.spatial.global_position.y - probe.position.y),
        static_cast<float>(instance.spatial.global_position.z - probe.position.z),
    };
    const world::AuthoredRotation inverse_probe{
        -probe.authored.rotation.x,
        -probe.authored.rotation.y,
        -probe.authored.rotation.z,
        probe.authored.rotation.w,
    };
    const auto inverse_probe_rotation = math::rotation({
        inverse_probe.x,
        inverse_probe.y,
        inverse_probe.z,
        inverse_probe.w,
    });
    const auto instance_rotation = math::rotation({
        instance.authored.rotation.x,
        instance.authored.rotation.y,
        instance.authored.rotation.z,
        instance.authored.rotation.w,
    });
    const auto instance_linear = math::multiply(
        instance_rotation,
        scale_matrix(instance.authored.scale)
    );
    return math::multiply(
        inverse_probe_rotation,
        math::multiply(math::translation(relative_origin), instance_linear)
    );
}

[[nodiscard]] std::unique_ptr<render::Buffer> create_buffer_with_data(
    render::Device& device,
    const render::BufferUsage usage,
    const std::span<const std::byte> data,
    const std::string_view debug_name
) {
    if (data.empty()) return nullptr;
    render::BufferDesc desc;
    desc.size = data.size();
    desc.usage = usage;
    desc.memory = render::MemoryLocation::cpu_to_gpu;
    desc.debug_name = debug_name;
    auto buffer = device.create_buffer(desc);
    if (!buffer || !buffer->write(0, data)) return nullptr;
    return buffer;
}

[[nodiscard]] std::unique_ptr<render::Buffer> create_gpu_default_frame_buffer(
    render::Device& device
) {
    render::BufferDesc desc;
    desc.size = sizeof(GpuDefaultFrame);
    desc.usage = render::BufferUsage::uniform;
    desc.memory = render::MemoryLocation::cpu_to_gpu;
    desc.debug_name = "VICE Runtime GPU default frame constants";
    return device.create_buffer(desc);
}

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

[[nodiscard]] std::unique_ptr<render::Buffer> create_gpu_pbr_frame_buffer(
    render::Device& device
) {
    render::BufferDesc desc;
    desc.size = sizeof(GpuPbrSceneConstants);
    desc.usage = render::BufferUsage::uniform;
    desc.memory = render::MemoryLocation::cpu_to_gpu;
    desc.debug_name = "VICE Runtime GPU PBR scene constants";
    return device.create_buffer(desc);
}

[[nodiscard]] std::unique_ptr<render::Buffer> create_constant_buffer(render::Device& device) {
    render::BufferDesc desc;
    desc.size = sizeof(SceneConstants);
    desc.usage = render::BufferUsage::uniform;
    desc.memory = render::MemoryLocation::cpu_to_gpu;
    desc.debug_name = "VICE Runtime scene constants";
    return device.create_buffer(desc);
}

[[nodiscard]] std::unique_ptr<render::Buffer> create_motion_constant_buffer(
    render::Device& device
) {
    render::BufferDesc desc;
    desc.size = sizeof(MotionConstants);
    desc.usage = render::BufferUsage::uniform;
    desc.memory = render::MemoryLocation::cpu_to_gpu;
    desc.debug_name = "VICE Runtime motion-vector constants";
    return device.create_buffer(desc);
}

[[nodiscard]] std::unique_ptr<render::Buffer> create_taa_constant_buffer(
    render::Device& device
) {
    render::BufferDesc desc;
    desc.size = sizeof(TaaConstants);
    desc.usage = render::BufferUsage::uniform;
    desc.memory = render::MemoryLocation::cpu_to_gpu;
    desc.debug_name = "VICE Runtime TAA constants";
    return device.create_buffer(desc);
}

[[nodiscard]] std::unique_ptr<render::Buffer> create_bloom_constant_buffer(
    render::Device& device
) {
    render::BufferDesc desc;
    desc.size = sizeof(BloomConstants);
    desc.usage = render::BufferUsage::uniform;
    desc.memory = render::MemoryLocation::cpu_to_gpu;
    desc.debug_name = "VICE Runtime bloom constants";
    return device.create_buffer(desc);
}

[[nodiscard]] bool write_bloom_constants(
    render::Buffer& buffer,
    const std::uint32_t width,
    const std::uint32_t height,
    const float threshold,
    const float soft_knee,
    const float strength,
    const float direction_x,
    const float direction_y
) {
    const BloomConstants constants{
        .texel_size = {
            1.0F / static_cast<float>(std::max(1U, width)),
            1.0F / static_cast<float>(std::max(1U, height)),
        },
        .threshold = threshold,
        .soft_knee = soft_knee,
        .strength = strength,
        .direction = {direction_x, direction_y},
    };
    return buffer.write(0U, std::as_bytes(std::span{&constants, 1}));
}

[[nodiscard]] std::unique_ptr<render::Buffer> create_sky_constant_buffer(
    render::Device& device
) {
    render::BufferDesc desc;
    desc.size = sizeof(detail::SkyConstants);
    desc.usage = render::BufferUsage::uniform;
    desc.memory = render::MemoryLocation::cpu_to_gpu;
    desc.debug_name = "VICE Runtime sky constants";
    return device.create_buffer(desc);
}

[[nodiscard]] std::unique_ptr<render::Buffer> create_tone_map_constant_buffer(
    render::Device& device
) {
    render::BufferDesc desc;
    desc.size = sizeof(ToneMapConstants);
    desc.usage = render::BufferUsage::uniform;
    desc.memory = render::MemoryLocation::cpu_to_gpu;
    desc.debug_name = "VICE Runtime tone-map constants";
    return device.create_buffer(desc);
}

[[nodiscard]] float mesh_radius(const render::MeshAsset& mesh) noexcept {
    float radius_squared = 0.0F;
    for (const auto& vertex : mesh.vertices) {
        const float x = vertex.position[0];
        const float y = vertex.position[1];
        const float z = vertex.position[2];
        radius_squared = std::max(radius_squared, x * x + y * y + z * z);
    }
    return std::sqrt(radius_squared);
}

[[nodiscard]] RuntimeCameraView fallback_camera(
    const std::span<const RenderMeshInstance> instances,
    const CookedMeshCatalog& catalog,
    const float aspect
) noexcept {
    if (instances.empty()) {
        const math::Vec3 target{};
        const math::Vec3 eye{6.0F, 4.5F, 8.0F};
        const math::Vec3 forward = math::normalize(target - eye);
        const math::Vec3 up{0.0F, 1.0F, 0.0F};
        const world::Camera camera{
            .projection = world::CameraProjection::perspective,
            .vertical_fov_radians = runtime_fov,
            .near_plane = 0.05F,
            .far_plane = 250.0F,
            .primary = false,
        };
        return RuntimeCameraView{
            .camera = camera,
            .position = eye,
            .forward = forward,
            .up = up,
            .aspect_ratio = aspect,
            .view_projection = math::multiply(
                math::perspective_rh_zo(
                    camera.vertical_fov_radians,
                    aspect,
                    camera.near_plane,
                    camera.far_plane
                ),
                math::look_at_rh(eye, target, up)
            ),
        };
    }

    math::Vec3 minimum{
        static_cast<float>(instances.front().spatial.global_position.x),
        static_cast<float>(instances.front().spatial.global_position.y),
        static_cast<float>(instances.front().spatial.global_position.z),
    };
    math::Vec3 maximum = minimum;

    for (const auto& instance : instances) {
        const math::Vec3 position{
            static_cast<float>(instance.spatial.global_position.x),
            static_cast<float>(instance.spatial.global_position.y),
            static_cast<float>(instance.spatial.global_position.z),
        };
        float radius = 0.5F;
        if (const auto* record = catalog.find(instance.mesh); record != nullptr) {
            const float scale = std::max({
                std::abs(instance.authored.scale.x),
                std::abs(instance.authored.scale.y),
                std::abs(instance.authored.scale.z),
            });
            radius = std::max(radius, mesh_radius(record->asset) * scale);
        }
        minimum.x = std::min(minimum.x, position.x - radius);
        minimum.y = std::min(minimum.y, position.y - radius);
        minimum.z = std::min(minimum.z, position.z - radius);
        maximum.x = std::max(maximum.x, position.x + radius);
        maximum.y = std::max(maximum.y, position.y + radius);
        maximum.z = std::max(maximum.z, position.z + radius);
    }

    const math::Vec3 target{
        (minimum.x + maximum.x) * 0.5F,
        (minimum.y + maximum.y) * 0.5F,
        (minimum.z + maximum.z) * 0.5F,
    };
    const math::Vec3 extent{
        maximum.x - minimum.x,
        maximum.y - minimum.y,
        maximum.z - minimum.z,
    };
    const float radius = std::max(1.0F, 0.5F * std::sqrt(math::dot(extent, extent)));
    const float distance = std::max(5.0F, radius / std::tan(runtime_fov * 0.5F) * 1.35F);
    const math::Vec3 direction = math::normalize({1.0F, 0.7F, 1.0F});
    const math::Vec3 eye = add(target, scaled(direction, distance));
    const math::Vec3 forward = math::normalize(target - eye);
    const math::Vec3 up{0.0F, 1.0F, 0.0F};
    const float far_plane = std::max(250.0F, distance + radius * 6.0F);
    const world::Camera camera{
        .projection = world::CameraProjection::perspective,
        .vertical_fov_radians = runtime_fov,
        .near_plane = 0.05F,
        .far_plane = far_plane,
        .primary = false,
    };

    return RuntimeCameraView{
        .camera = camera,
        .position = eye,
        .forward = forward,
        .up = up,
        .aspect_ratio = aspect,
        .view_projection = math::multiply(
            math::perspective_rh_zo(
                camera.vertical_fov_radians,
                aspect,
                camera.near_plane,
                camera.far_plane
            ),
            math::look_at_rh(eye, target, up)
        ),
    };
}

void set_diagnostics(std::string* const diagnostics, std::string message) {
    if (diagnostics != nullptr) *diagnostics = std::move(message);
}

} // namespace

bool CookedMeshCatalog::load(const std::filesystem::path& content_root) {
    records_.clear();
    diagnostics_.clear();

    std::error_code error;
    if (content_root.empty() || !std::filesystem::is_directory(content_root, error)) {
        diagnostics_ = error
            ? "Could not inspect runtime content root: " + error.message()
            : "Runtime content root is not available.";
        return false;
    }

    std::unordered_set<world::ResourceId, world::ResourceIdHash> seen;
    std::filesystem::recursive_directory_iterator iterator{
        content_root,
        std::filesystem::directory_options::skip_permission_denied,
        error,
    };
    const std::filesystem::recursive_directory_iterator end;
    if (error) {
        diagnostics_ = "Could not enumerate runtime content: " + error.message();
        return false;
    }

    for (; iterator != end; iterator.increment(error)) {
        if (error) {
            diagnostics_ = "Could not enumerate runtime content: " + error.message();
            return false;
        }
        if (!iterator->is_regular_file() || iterator->path().extension() != ".vmeshc") continue;

        const auto loaded = render::load_mesh_asset(iterator->path());
        if (!loaded.asset.has_value()) {
            diagnostics_ = "Could not load cooked runtime mesh `" +
                iterator->path().generic_string() + "`: " + loaded.diagnostics;
            return false;
        }
        if (!render::valid(loaded.asset->stable_id)) {
            diagnostics_ = "Cooked runtime mesh has no stable ID: " +
                iterator->path().generic_string();
            return false;
        }

        const world::ResourceId resource{world::PersistentId{loaded.asset->stable_id}};
        if (!seen.insert(resource).second) {
            diagnostics_ = "Duplicate cooked runtime mesh stable ID `" +
                resource.value.to_string() + "`.";
            return false;
        }
        records_.push_back(CookedMeshRecord{
            .resource_id = resource,
            .path = iterator->path(),
            .asset = std::move(*loaded.asset),
        });
    }

    return true;
}

const CookedMeshRecord* CookedMeshCatalog::find(const world::ResourceId resource_id) const noexcept {
    const auto iterator = std::find_if(
        records_.begin(),
        records_.end(),
        [resource_id](const CookedMeshRecord& record) {
            return record.resource_id == resource_id;
        }
    );
    return iterator == records_.end() ? nullptr : &*iterator;
}

std::span<const CookedMeshRecord> CookedMeshCatalog::records() const noexcept {
    return records_;
}

const std::string& CookedMeshCatalog::diagnostics() const noexcept {
    return diagnostics_;
}

std::vector<RenderMeshInstance> collect_render_mesh_instances(const world::World& world) {
    std::vector<RenderMeshInstance> instances;
    const auto snapshot = world.snapshot();
    for (const auto& entity : snapshot.entities) {
        if (!entity.spatial_state.has_value()) continue;
        std::optional<world::ResourceId> mesh;
        std::optional<world::ResourceId> material;
        for (const auto& reference : entity.resource_references) {
            if (!reference.resource.valid()) continue;
            if (reference.role == world::ResourceRole::render_mesh) mesh = reference.resource;
            if (reference.role == world::ResourceRole::render_material) material = reference.resource;
        }
        if (!mesh.has_value()) continue;
        instances.push_back(RenderMeshInstance{
            .entity = entity.persistent_id,
            .mesh = *mesh,
            .material = material,
            .spatial = *entity.spatial_state,
            .authored = entity.authored_transform.value_or(world::AuthoredTransform{}),
        });
    }
    return instances;
}

std::vector<RuntimeLight> collect_runtime_lights(const world::World& world) {
    std::vector<RuntimeLight> lights;
    lights.reserve(max_runtime_lights);
    const auto snapshot = world.snapshot();
    for (const auto& entity : snapshot.entities) {
        if (lights.size() >= max_runtime_lights) break;
        if (!entity.spatial_state.has_value() || !entity.light.has_value() || !entity.light->enabled) {
            continue;
        }

        const auto authored = entity.authored_transform.value_or(world::AuthoredTransform{});
        RuntimeLight light{
            .entity = entity.persistent_id,
            .light = *entity.light,
            .position = {
                static_cast<float>(entity.spatial_state->global_position.x),
                static_cast<float>(entity.spatial_state->global_position.y),
                static_cast<float>(entity.spatial_state->global_position.z),
            },
            .global_position = entity.spatial_state->global_position,
        };
        if (light.light.type == world::LightType::directional) {
            light.direction = math::normalize(
                rotate_vector(authored.rotation, {0.0F, 0.0F, -1.0F})
            );
        }
        lights.push_back(light);
    }
    return lights;
}


RuntimeAtmosphereFrame runtime_atmosphere_frame(
    const std::span<const RuntimeLight> lights
) noexcept {
    RuntimeAtmosphereFrame frame;
    for (const auto& source : lights) {
        if (!source.light.enabled || source.light.type != world::LightType::directional) continue;
        const math::Vec3 direction = math::normalize(source.direction);
        if (math::dot(direction, direction) < 0.99F) continue;
        frame.sun_direction = {-direction.x, -direction.y, -direction.z};
        frame.sun_color = {
            source.light.color.red,
            source.light.color.green,
            source.light.color.blue,
        };
        frame.sun_intensity = source.light.intensity;
        break;
    }
    return frame;
}

float runtime_atmosphere_transmittance(
    const float distance,
    const RuntimeAtmosphereSettings& settings
) noexcept {
    if (!std::isfinite(distance) || distance <= 0.0F) return 1.0F;
    const float density = std::isfinite(settings.aerial_density)
        ? std::max(settings.aerial_density, 0.0F)
        : 0.0F;
    return std::clamp(std::exp(-density * distance), 0.0F, 1.0F);
}

math::Vec3 evaluate_runtime_sky_radiance(
    math::Vec3 view_direction,
    const RuntimeAtmosphereFrame& atmosphere
) noexcept {
    constexpr float pi = 3.14159265358979323846F;
    view_direction = math::normalize(view_direction);
    if (math::dot(view_direction, view_direction) < 0.99F) return {};
    math::Vec3 sun_direction = math::normalize(atmosphere.sun_direction);
    if (math::dot(sun_direction, sun_direction) < 0.99F) {
        sun_direction = {0.431934F, 0.863868F, 0.259161F};
    }

    const float sun_intensity = std::max(atmosphere.sun_intensity, 0.0F);
    const auto& settings = atmosphere.settings;
    const float rayleigh_strength = std::max(settings.rayleigh_strength, 0.0F);
    const float mie_strength = std::max(settings.mie_strength, 0.0F);
    const float g = std::clamp(settings.mie_anisotropy, 0.0F, 0.98F);
    const float sun_radius = std::max(settings.sun_angular_radius, 0.0001F);
    const float horizon_falloff = std::max(settings.horizon_falloff, 0.01F);
    const float mu = std::clamp(math::dot(view_direction, sun_direction), -1.0F, 1.0F);
    const float rayleigh_phase = (3.0F / (16.0F * pi)) * (1.0F + mu * mu);
    const float mie_denominator = std::max(1.0F + g * g - 2.0F * g * mu, 0.0001F);
    const float mie_phase = ((1.0F - g * g) / (4.0F * pi)) /
        std::pow(mie_denominator, 1.5F);
    const float altitude = std::max(view_direction.y, 0.0F);
    const float air_mass = 1.0F / std::max(0.10F + altitude * 0.90F, 0.10F);
    const float beta_mie = 0.021F * mie_strength;
    const float horizon = std::exp(-altitude / horizon_falloff);
    const float disk_inner = std::cos(sun_radius * 0.55F);
    const float disk_outer = std::cos(sun_radius * 2.2F);
    const float disk_t = std::clamp(
        (mu - disk_outer) / std::max(disk_inner - disk_outer, 0.000001F),
        0.0F,
        1.0F
    );
    const float sun_disk = disk_t * disk_t * (3.0F - 2.0F * disk_t);
    const float sky_t = std::clamp((view_direction.y + 0.08F) / 0.105F, 0.0F, 1.0F);
    const float sky_weight = sky_t * sky_t * (3.0F - 2.0F * sky_t);
    const float sun_height = std::clamp(sun_direction.y * 0.5F + 0.5F, 0.0F, 1.0F);

    const float beta_rayleigh[3]{
        0.0058F * rayleigh_strength,
        0.0135F * rayleigh_strength,
        0.0331F * rayleigh_strength,
    };
    const float sun_color[3]{
        std::max(atmosphere.sun_color.x, 0.0F),
        std::max(atmosphere.sun_color.y, 0.0F),
        std::max(atmosphere.sun_color.z, 0.0F),
    };
    const float ground[3]{
        std::max(settings.ground_albedo.x, 0.0F),
        std::max(settings.ground_albedo.y, 0.0F),
        std::max(settings.ground_albedo.z, 0.0F),
    };
    float output[3]{};
    for (std::size_t channel = 0U; channel < 3U; ++channel) {
        const float extinction = std::exp(
            -(beta_rayleigh[channel] + beta_mie) * air_mass * 1.35F
        );
        const float scatter =
            (beta_rayleigh[channel] * rayleigh_phase + beta_mie * mie_phase) *
            air_mass * 24.0F * (0.35F + 0.65F * (1.0F - extinction));
        float sky = sun_color[channel] * sun_intensity * scatter;
        const float horizon_tint = channel == 0U ? 0.018F : (channel == 1U ? 0.011F : 0.006F);
        sky += sun_color[channel] * sun_intensity * horizon * horizon_tint;
        sky += sun_color[channel] * sun_intensity * sun_disk * 7.5F;
        const float ground_radiance = ground[channel] * sun_color[channel] *
            (0.12F + sun_intensity * 0.07F * sun_height);
        output[channel] = std::max(
            ground_radiance + (sky - ground_radiance) * sky_weight,
            0.0F
        );
    }
    return {output[0], output[1], output[2]};
}

std::vector<RuntimeReflectionProbe> collect_runtime_reflection_probes(const world::World& world) {
    std::vector<RuntimeReflectionProbe> probes;
    const auto snapshot = world.snapshot();
    for (const auto& entity : snapshot.entities) {
        if (!entity.spatial_state.has_value() || !entity.reflection_probe.has_value() ||
            !entity.reflection_probe->enabled) continue;
        std::optional<world::ResourceId> environment;
        for (const auto& reference : entity.resource_references) {
            if (reference.role == world::ResourceRole::reflection_environment && reference.resource.valid()) {
                environment = reference.resource;
                break;
            }
        }
        if (!environment.has_value()) continue;
        probes.push_back(RuntimeReflectionProbe{
            .entity = entity.persistent_id,
            .probe = *entity.reflection_probe,
            .environment = *environment,
            .position = entity.spatial_state->global_position,
            .authored = entity.authored_transform.value_or(world::AuthoredTransform{}),
        });
    }
    return probes;
}

RuntimeReflectionBlend select_runtime_reflection_probes(
    const world::GlobalPosition point,
    const std::span<const RuntimeReflectionProbe> probes
) noexcept {
    struct Candidate {
        world::ResourceId environment{};
        world::PersistentId entity{};
        std::size_t probe_index = 0U;
        float weight = 0.0F;
        std::int32_t priority = 0;
    };
    std::vector<Candidate> candidates;
    for (std::size_t probe_index = 0U; probe_index < probes.size(); ++probe_index) {
        const auto& probe = probes[probe_index];
        if (!probe.probe.enabled || !world::valid(probe.probe) || !probe.environment.valid()) continue;
        const math::Vec3 offset{
            static_cast<float>(point.x - probe.position.x),
            static_cast<float>(point.y - probe.position.y),
            static_cast<float>(point.z - probe.position.z),
        };
        const world::AuthoredRotation inverse{
            -probe.authored.rotation.x,
            -probe.authored.rotation.y,
            -probe.authored.rotation.z,
            probe.authored.rotation.w,
        };
        const math::Vec3 local = rotate_vector(inverse, offset);
        const math::Vec3 extent{
            probe.probe.half_extents.x * probe.authored.scale.x,
            probe.probe.half_extents.y * probe.authored.scale.y,
            probe.probe.half_extents.z * probe.authored.scale.z,
        };
        if (std::abs(local.x) > extent.x || std::abs(local.y) > extent.y || std::abs(local.z) > extent.z) {
            continue;
        }
        const float edge = std::min({
            extent.x - std::abs(local.x),
            extent.y - std::abs(local.y),
            extent.z - std::abs(local.z),
        });
        const float weight = probe.probe.blend_distance <= 0.0F
            ? 1.0F
            : std::clamp(edge / probe.probe.blend_distance, 0.0F, 1.0F);
        if (weight <= 0.0F) continue;
        candidates.push_back(Candidate{
            probe.environment,
            probe.entity,
            probe_index,
            weight,
            probe.probe.priority,
        });
    }

    RuntimeReflectionBlend result;
    if (candidates.empty()) return result;
    const auto best_priority = std::max_element(candidates.begin(), candidates.end(),
        [](const Candidate& left, const Candidate& right) { return left.priority < right.priority; })->priority;
    std::erase_if(candidates, [best_priority](const Candidate& candidate) {
        return candidate.priority != best_priority;
    });
    std::sort(candidates.begin(), candidates.end(), [](const Candidate& left, const Candidate& right) {
        if (left.weight != right.weight) return left.weight > right.weight;
        return std::lexicographical_compare(
            left.entity.bytes().begin(), left.entity.bytes().end(),
            right.entity.bytes().begin(), right.entity.bytes().end()
        );
    });

    float local_sum = 0.0F;
    for (std::size_t index = 0U; index < result.local_environments.size() && index < candidates.size(); ++index) {
        result.local_environments[index] = candidates[index].environment;
        result.local_probe_indices[index] = candidates[index].probe_index;
        result.local_weights[index] = candidates[index].weight;
        local_sum += candidates[index].weight;
    }
    if (local_sum > 1.0F) {
        const float inverse = 1.0F / local_sum;
        for (float& weight : result.local_weights) weight *= inverse;
        result.global_weight = 0.0F;
    } else {
        result.global_weight = 1.0F - local_sum;
    }
    return result;
}

math::Vec3 parallax_correct_reflection_direction(
    const world::GlobalPosition point,
    const math::Vec3 world_direction,
    const RuntimeReflectionProbe& probe
) noexcept {
    const math::Vec3 direction = math::normalize(world_direction);
    if (!probe.probe.enabled || !world::valid(probe.probe) ||
        math::dot(direction, direction) <= 0.0F) {
        return direction;
    }

    const math::Vec3 world_offset{
        static_cast<float>(point.x - probe.position.x),
        static_cast<float>(point.y - probe.position.y),
        static_cast<float>(point.z - probe.position.z),
    };
    const world::AuthoredRotation inverse{
        -probe.authored.rotation.x,
        -probe.authored.rotation.y,
        -probe.authored.rotation.z,
        probe.authored.rotation.w,
    };
    const math::Vec3 local_position = rotate_vector(inverse, world_offset);
    const math::Vec3 local_direction = math::normalize(rotate_vector(inverse, direction));
    const math::Vec3 extent{
        probe.probe.half_extents.x * probe.authored.scale.x,
        probe.probe.half_extents.y * probe.authored.scale.y,
        probe.probe.half_extents.z * probe.authored.scale.z,
    };
    if (extent.x <= 0.0F || extent.y <= 0.0F || extent.z <= 0.0F ||
        std::abs(local_position.x) > extent.x ||
        std::abs(local_position.y) > extent.y ||
        std::abs(local_position.z) > extent.z) {
        return direction;
    }

    float distance = std::numeric_limits<float>::infinity();
    const auto intersect_axis = [&](const float position, const float ray, const float half_extent) {
        if (std::abs(ray) <= 1.0e-6F) return std::numeric_limits<float>::infinity();
        const float first = (-half_extent - position) / ray;
        const float second = (half_extent - position) / ray;
        return std::max(first, second);
    };
    distance = std::min(distance, intersect_axis(local_position.x, local_direction.x, extent.x));
    distance = std::min(distance, intersect_axis(local_position.y, local_direction.y, extent.y));
    distance = std::min(distance, intersect_axis(local_position.z, local_direction.z, extent.z));
    if (!std::isfinite(distance) || distance <= 0.0F) return direction;

    const math::Vec3 local_hit = add(local_position, scaled(local_direction, distance));
    if (math::dot(local_hit, local_hit) <= 1.0e-10F) return direction;
    return math::normalize(rotate_vector(probe.authored.rotation, math::normalize(local_hit)));
}

std::optional<std::size_t> select_runtime_shadow_light(
    const std::span<const RuntimeLight> lights
) noexcept {
    for (std::size_t index = 0; index < lights.size(); ++index) {
        if (lights[index].light.enabled &&
            lights[index].light.type == world::LightType::directional) {
            return index;
        }
    }
    return std::nullopt;
}

std::array<float, runtime_shadow_cascade_count> runtime_shadow_cascade_splits(
    const RuntimeCameraView& camera
) noexcept {
    std::array<float, runtime_shadow_cascade_count> splits{};
    const float near_plane = std::max(0.001F, camera.camera.near_plane);
    const float far_plane = std::min(camera.camera.far_plane, runtime_shadow_distance);
    if (!std::isfinite(camera.camera.vertical_fov_radians) ||
        !std::isfinite(camera.aspect_ratio) ||
        !std::isfinite(near_plane) || !std::isfinite(far_plane) ||
        camera.camera.vertical_fov_radians <= 0.0F || camera.aspect_ratio <= 0.0F ||
        far_plane <= near_plane) {
        return splits;
    }

    for (std::size_t index = 0; index < splits.size(); ++index) {
        const float ratio = static_cast<float>(index + 1U) /
            static_cast<float>(splits.size());
        const float logarithmic = near_plane * std::pow(far_plane / near_plane, ratio);
        const float uniform = near_plane + (far_plane - near_plane) * ratio;
        splits[index] = runtime_shadow_split_lambda * logarithmic +
            (1.0F - runtime_shadow_split_lambda) * uniform;
    }
    splits.back() = far_plane;
    return splits;
}

namespace {

[[nodiscard]] std::array<math::Vec3, 8U> camera_slice_corners(
    const RuntimeCameraView& camera,
    const float near_plane,
    const float far_plane
) noexcept {
    std::array<math::Vec3, 8U> corners{};
    const math::Vec3 forward = math::normalize(camera.forward);
    const math::Vec3 up = math::normalize(camera.up);
    const math::Vec3 right = math::normalize(math::cross(forward, up));
    const float tangent = std::tan(camera.camera.vertical_fov_radians * 0.5F);

    const auto write_plane = [&](const float distance, const std::size_t offset) {
        const float half_height = tangent * distance;
        const float half_width = half_height * camera.aspect_ratio;
        const math::Vec3 center = add(camera.position, scaled(forward, distance));
        const math::Vec3 horizontal = scaled(right, half_width);
        const math::Vec3 vertical = scaled(up, half_height);
        corners[offset + 0U] = add(add(center, horizontal), vertical);
        corners[offset + 1U] = add(add(center, scaled(horizontal, -1.0F)), vertical);
        corners[offset + 2U] = add(add(center, horizontal), scaled(vertical, -1.0F));
        corners[offset + 3U] = add(
            add(center, scaled(horizontal, -1.0F)),
            scaled(vertical, -1.0F)
        );
    };

    write_plane(near_plane, 0U);
    write_plane(far_plane, 4U);
    return corners;
}

[[nodiscard]] RuntimeShadowCascade fitted_shadow_cascade(
    const RuntimeCameraView& camera,
    const RuntimeLight& light,
    const float split_near,
    const float split_far
) noexcept {
    const auto corners = camera_slice_corners(camera, split_near, split_far);
    math::Vec3 center{};
    for (const auto corner : corners) center = add(center, scaled(corner, 1.0F / 8.0F));

    float radius = 1.0F;
    for (const auto corner : corners) {
        const math::Vec3 offset = corner - center;
        radius = std::max(radius, std::sqrt(math::dot(offset, offset)));
    }
    radius *= 1.05F;
    radius = std::ceil(radius * 16.0F) / 16.0F;

    const math::Vec3 direction = math::normalize(light.direction);
    const math::Vec3 backward = scaled(direction, -1.0F);
    math::Vec3 up_reference{0.0F, 1.0F, 0.0F};
    if (std::abs(math::dot(direction, up_reference)) > 0.95F) {
        up_reference = {0.0F, 0.0F, 1.0F};
    }
    const math::Vec3 light_right = math::normalize(math::cross(up_reference, backward));
    const math::Vec3 light_up = math::cross(backward, light_right);

    const float texel_size = (radius * 2.0F) /
        static_cast<float>(runtime_shadow_map_size);
    const float center_right = math::dot(light_right, center);
    const float center_up = math::dot(light_up, center);
    const float snapped_right = std::round(center_right / texel_size) * texel_size;
    const float snapped_up = std::round(center_up / texel_size) * texel_size;
    const math::Vec3 snapped_center = add(
        center,
        add(
            scaled(light_right, snapped_right - center_right),
            scaled(light_up, snapped_up - center_up)
        )
    );

    const float distance = radius * 4.0F + 50.0F;
    const math::Vec3 eye = add(snapped_center, scaled(direction, -distance));
    const float near_depth = 0.1F;
    const float far_depth = distance * 2.0F;
    return RuntimeShadowCascade{
        .split_near = split_near,
        .split_far = split_far,
        .view_projection = math::multiply(
            math::orthographic_rh_zo(
                -radius,
                radius,
                -radius,
                radius,
                near_depth,
                far_depth
            ),
            math::look_at_rh(eye, snapped_center, up_reference)
        ),
    };
}

} // namespace

RuntimeShadowCascadeBlend runtime_shadow_cascade_blend(
    const float world_depth,
    const std::array<float, runtime_shadow_cascade_count>& splits
) noexcept {
    RuntimeShadowCascadeBlend result{};
    const float depth = std::isfinite(world_depth) ? std::max(world_depth, 0.0F) : 0.0F;
    while (result.primary + 1U < splits.size() && depth > splits[result.primary]) {
        ++result.primary;
    }
    result.secondary = result.primary;
    if (result.primary + 1U >= splits.size()) return result;

    const float split_near = result.primary == 0U ? 0.0F : splits[result.primary - 1U];
    const float split_far = splits[result.primary];
    const float span = split_far - split_near;
    if (!std::isfinite(span) || span <= 0.0001F) return result;
    const float width = span * runtime_shadow_transition_fraction;
    if (width <= 0.0001F) return result;
    const float transition_start = split_far - width;
    if (depth <= transition_start) return result;

    result.secondary = result.primary + 1U;
    result.secondary_weight = std::clamp(
        (depth - transition_start) / width,
        0.0F,
        1.0F
    );
    return result;
}

std::optional<RuntimeShadowView> directional_shadow_view(
    const RuntimeCameraView& camera,
    const std::span<const RuntimeLight> lights
) noexcept {
    const auto light_index = select_runtime_shadow_light(lights);
    if (!light_index.has_value()) return std::nullopt;

    const auto& light = lights[*light_index];
    const math::Vec3 direction = math::normalize(light.direction);
    if (math::dot(direction, direction) < 0.99F) return std::nullopt;

    const auto splits = runtime_shadow_cascade_splits(camera);
    if (splits.front() <= camera.camera.near_plane || splits.back() <= splits.front()) {
        return std::nullopt;
    }

    RuntimeShadowView result{
        .light_entity = light.entity,
        .light_index = *light_index,
    };
    const float camera_near = std::max(0.001F, camera.camera.near_plane);
    for (std::size_t index = 0; index < result.cascades.size(); ++index) {
        float fit_near = index == 0U ? camera_near : splits[index - 1U];
        if (index > 0U) {
            const float previous_near = index == 1U ? 0.0F : splits[index - 2U];
            const float previous_span = splits[index - 1U] - previous_near;
            fit_near = std::max(
                camera_near,
                splits[index - 1U] - previous_span * runtime_shadow_transition_fraction
            );
        }
        result.cascades[index] = fitted_shadow_cascade(
            camera,
            light,
            fit_near,
            splits[index]
        );
    }
    return result;
}

RuntimePointShadowFace runtime_point_shadow_face(const math::Vec3 light_to_point) noexcept {
    const float ax = std::abs(light_to_point.x);
    const float ay = std::abs(light_to_point.y);
    const float az = std::abs(light_to_point.z);
    if (ax >= ay && ax >= az) {
        return light_to_point.x >= 0.0F
            ? RuntimePointShadowFace::positive_x : RuntimePointShadowFace::negative_x;
    }
    if (ay >= az) {
        return light_to_point.y >= 0.0F
            ? RuntimePointShadowFace::positive_y : RuntimePointShadowFace::negative_y;
    }
    return light_to_point.z >= 0.0F
        ? RuntimePointShadowFace::positive_z : RuntimePointShadowFace::negative_z;
}

math::Mat4 runtime_point_shadow_face_view_projection(
    const RuntimePointShadowFace face,
    const float range
) noexcept {
    if (!std::isfinite(range) || range <= 0.02F) return math::identity();
    math::Vec3 direction{};
    math::Vec3 up{0.0F, -1.0F, 0.0F};
    switch (face) {
    case RuntimePointShadowFace::positive_x: direction = {1.0F, 0.0F, 0.0F}; break;
    case RuntimePointShadowFace::negative_x: direction = {-1.0F, 0.0F, 0.0F}; break;
    case RuntimePointShadowFace::positive_y:
        direction = {0.0F, 1.0F, 0.0F}; up = {0.0F, 0.0F, 1.0F}; break;
    case RuntimePointShadowFace::negative_y:
        direction = {0.0F, -1.0F, 0.0F}; up = {0.0F, 0.0F, -1.0F}; break;
    case RuntimePointShadowFace::positive_z: direction = {0.0F, 0.0F, 1.0F}; break;
    case RuntimePointShadowFace::negative_z: direction = {0.0F, 0.0F, -1.0F}; break;
    }
    const float near_plane = std::clamp(range * 0.002F, 0.02F, range * 0.25F);
    constexpr float half_pi = 1.57079632679489661923F;
    return math::multiply(
        math::perspective_rh_zo(half_pi, 1.0F, near_plane, range),
        math::look_at_rh({}, direction, up)
    );
}

std::array<std::optional<RuntimePointShadowView>, runtime_point_shadow_light_count>
runtime_point_shadow_views(
    const world::GlobalPosition camera_position,
    const std::span<const RuntimeLight> lights
) noexcept {
    struct Candidate {
        std::size_t light_index = 0U;
        double normalized_distance_squared = 0.0;
        float intensity = 0.0F;
    };
    std::vector<Candidate> candidates;
    for (std::size_t index = 0U; index < lights.size(); ++index) {
        const auto& light = lights[index];
        if (!light.light.enabled || light.light.type != world::LightType::point ||
            !std::isfinite(light.light.range) || light.light.range <= 0.02F) continue;
        const double dx = light.global_position.x - camera_position.x;
        const double dy = light.global_position.y - camera_position.y;
        const double dz = light.global_position.z - camera_position.z;
        const double range = static_cast<double>(light.light.range);
        candidates.push_back(Candidate{
            .light_index = index,
            .normalized_distance_squared = (dx * dx + dy * dy + dz * dz) / (range * range),
            .intensity = light.light.intensity,
        });
    }
    std::sort(candidates.begin(), candidates.end(), [&](const Candidate& left, const Candidate& right) {
        if (left.normalized_distance_squared != right.normalized_distance_squared) {
            return left.normalized_distance_squared < right.normalized_distance_squared;
        }
        if (left.intensity != right.intensity) return left.intensity > right.intensity;
        const auto& left_id = lights[left.light_index].entity.bytes();
        const auto& right_id = lights[right.light_index].entity.bytes();
        if (left_id != right_id) {
            return std::lexicographical_compare(
                left_id.begin(), left_id.end(), right_id.begin(), right_id.end()
            );
        }
        return left.light_index < right.light_index;
    });

    std::array<std::optional<RuntimePointShadowView>, runtime_point_shadow_light_count> result{};
    const std::size_t count = std::min(result.size(), candidates.size());
    for (std::size_t slot = 0U; slot < count; ++slot) {
        const std::size_t light_index = candidates[slot].light_index;
        const auto& light = lights[light_index];
        RuntimePointShadowView view{
            .light_entity = light.entity,
            .light_index = light_index,
            .global_position = light.global_position,
            .range = light.light.range,
        };
        for (std::size_t face_index = 0U; face_index < runtime_point_shadow_face_count; ++face_index) {
            view.face_view_projections[face_index] = runtime_point_shadow_face_view_projection(
                static_cast<RuntimePointShadowFace>(face_index),
                view.range
            );
        }
        result[slot] = view;
    }
    return result;
}

math::Mat4 runtime_model_matrix_relative_to(
    const RenderMeshInstance& instance,
    const world::GlobalPosition origin
) noexcept {
    auto result = math::translation({
        static_cast<float>(instance.spatial.global_position.x - origin.x),
        static_cast<float>(instance.spatial.global_position.y - origin.y),
        static_cast<float>(instance.spatial.global_position.z - origin.z),
    });
    result = math::multiply(
        result,
        math::rotation({
            instance.authored.rotation.x,
            instance.authored.rotation.y,
            instance.authored.rotation.z,
            instance.authored.rotation.w,
        })
    );
    return math::multiply(result, scale_matrix(instance.authored.scale));
}

math::Mat4 runtime_model_matrix(const RenderMeshInstance& instance) noexcept {
    return runtime_model_matrix_relative_to(instance, {});
}

[[nodiscard]] GpuPbrSceneConstants make_gpu_pbr_scene_constants(
    const math::Mat4& view_projection,
    const RuntimeCameraView& runtime_camera,
    const std::span<const RuntimeLight> runtime_lights,
    const std::optional<RuntimeShadowView>& shadow_view,
    const std::array<
        std::optional<RuntimePointShadowView>,
        runtime_point_shadow_light_count
    >& point_shadow_views,
    const RuntimeAtmosphereFrame& atmosphere,
    const detail::RuntimeEnvironmentBinding& global_environment
) noexcept {
    GpuPbrSceneConstants constants{
        .view_projection = view_projection,
        .camera_position = {
            runtime_camera.position.x,
            runtime_camera.position.y,
            runtime_camera.position.z,
            1.0F,
        },
    };
    constants.light_metadata[0] = static_cast<float>(runtime_lights.size());
    for (std::size_t light_index = 0U; light_index < runtime_lights.size(); ++light_index) {
        const auto& source = runtime_lights[light_index];
        auto& destination = constants.lights[light_index];
        if (source.light.type == world::LightType::directional) {
            destination.position_or_direction[0] = source.direction.x;
            destination.position_or_direction[1] = source.direction.y;
            destination.position_or_direction[2] = source.direction.z;
            destination.position_or_direction[3] = 0.0F;
        } else {
            destination.position_or_direction[0] = source.position.x;
            destination.position_or_direction[1] = source.position.y;
            destination.position_or_direction[2] = source.position.z;
            destination.position_or_direction[3] = 1.0F;
        }
        destination.color_intensity[0] = source.light.color.red;
        destination.color_intensity[1] = source.light.color.green;
        destination.color_intensity[2] = source.light.color.blue;
        destination.color_intensity[3] = source.light.intensity;
        destination.parameters[0] = source.light.range;
    }

    if (shadow_view.has_value()) {
        constants.shadow_view_projection = shadow_view->cascades[0].view_projection;
        for (std::size_t cascade_index = 1U; cascade_index < runtime_shadow_cascade_count; ++cascade_index) {
            constants.shadow_cascade_view_projections[cascade_index - 1U] =
                shadow_view->cascades[cascade_index].view_projection;
        }
        for (std::size_t cascade_index = 0U; cascade_index < runtime_shadow_cascade_count; ++cascade_index) {
            constants.shadow_cascade_splits[cascade_index] =
                shadow_view->cascades[cascade_index].split_far;
        }
        constants.shadow_camera_forward[0] = runtime_camera.forward.x;
        constants.shadow_camera_forward[1] = runtime_camera.forward.y;
        constants.shadow_camera_forward[2] = runtime_camera.forward.z;
        constants.shadow_metadata[0] = 1.0F;
        constants.shadow_metadata[1] = static_cast<float>(shadow_view->light_index);
        constants.shadow_metadata[2] = 1.0F / static_cast<float>(runtime_shadow_map_size);
        constants.shadow_metadata[3] = 0.0015F;
    }

    if (global_environment.available) {
        constants.environment_metadata[0] = global_environment.intensity;
        constants.environment_metadata[1] = global_environment.rotation_radians;
        constants.environment_metadata[2] = global_environment.max_specular_lod;
        constants.environment_metadata[3] = 1.0F;
    }

    constants.atmosphere_sun_direction_intensity[0] = atmosphere.sun_direction.x;
    constants.atmosphere_sun_direction_intensity[1] = atmosphere.sun_direction.y;
    constants.atmosphere_sun_direction_intensity[2] = atmosphere.sun_direction.z;
    constants.atmosphere_sun_direction_intensity[3] = atmosphere.sun_intensity;
    constants.atmosphere_sun_color[0] = atmosphere.sun_color.x;
    constants.atmosphere_sun_color[1] = atmosphere.sun_color.y;
    constants.atmosphere_sun_color[2] = atmosphere.sun_color.z;
    constants.atmosphere_sun_color[3] = 1.0F;
    constants.atmosphere_parameters[0] = atmosphere.settings.rayleigh_strength;
    constants.atmosphere_parameters[1] = atmosphere.settings.mie_strength;
    constants.atmosphere_parameters[2] = atmosphere.settings.mie_anisotropy;
    constants.atmosphere_parameters[3] = atmosphere.settings.aerial_density;
    constants.atmosphere_secondary[0] = atmosphere.settings.sun_angular_radius;
    constants.atmosphere_secondary[1] = atmosphere.settings.horizon_falloff;
    constants.atmosphere_secondary[2] = 1.0F;
    constants.atmosphere_ground_albedo[0] = atmosphere.settings.ground_albedo.x;
    constants.atmosphere_ground_albedo[1] = atmosphere.settings.ground_albedo.y;
    constants.atmosphere_ground_albedo[2] = atmosphere.settings.ground_albedo.z;
    constants.atmosphere_ground_albedo[3] = 1.0F;

    for (std::size_t light_slot = 0U; light_slot < point_shadow_views.size(); ++light_slot) {
        if (!point_shadow_views[light_slot].has_value()) continue;
        const auto& point_view = *point_shadow_views[light_slot];
        auto& point_constants = constants.point_shadows[light_slot];
        point_constants.metadata[0] = 1.0F;
        point_constants.metadata[1] = static_cast<float>(point_view.light_index);
        point_constants.metadata[2] = point_view.range;
        point_constants.metadata[3] = 1.0F / static_cast<float>(runtime_point_shadow_map_size);
        point_constants.face_view_projections = point_view.face_view_projections;
    }
    return constants;
}

std::optional<RuntimeCameraView> primary_camera_view(
    const world::World& world,
    const float aspect_ratio
) noexcept {
    if (!std::isfinite(aspect_ratio) || aspect_ratio <= 0.0F) {
        return std::nullopt;
    }
    const auto entity = world.primary_camera();
    if (!entity.has_value()) {
        return std::nullopt;
    }
    const auto camera = world.camera(*entity);
    const auto spatial = world.spatial_state(*entity);
    const auto persistent_id = world.persistent_id(*entity);
    if (!camera.has_value() || !spatial.has_value() || !persistent_id.has_value()) {
        return std::nullopt;
    }

    const auto transform = world.authored_transform(*entity).value_or(world::AuthoredTransform{});
    const math::Vec3 eye{
        static_cast<float>(spatial->global_position.x),
        static_cast<float>(spatial->global_position.y),
        static_cast<float>(spatial->global_position.z),
    };
    const math::Vec3 forward = math::normalize(rotate_vector(transform.rotation, {0.0F, 0.0F, -1.0F}));
    const math::Vec3 up = math::normalize(rotate_vector(transform.rotation, {0.0F, 1.0F, 0.0F}));
    const math::Vec3 target = add(eye, forward);

    return RuntimeCameraView{
        .entity = *persistent_id,
        .camera = *camera,
        .position = eye,
        .forward = forward,
        .up = up,
        .aspect_ratio = aspect_ratio,
        .view_projection = math::multiply(
            math::perspective_rh_zo(
                camera->vertical_fov_radians,
                aspect_ratio,
                camera->near_plane,
                camera->far_plane
            ),
            math::look_at_rh(eye, target, up)
        ),
    };
}

struct RuntimeSceneRenderer::State {
    struct MeshGpu {
        std::unique_ptr<render::Buffer> vertices;
        std::unique_ptr<render::Buffer> indices;
        std::uint32_t index_count = 0;
        RuntimeBoundingSphere bounds{};
    };

    render::Device* device = nullptr;
    CookedMeshCatalog catalog;
    detail::RuntimeMaterialLibrary materials;
    detail::RuntimeEnvironment environment;
    RuntimeVisibilityCuller visibility;
    RuntimeHiZPyramid hiz_pyramid;
    RuntimeHiZOcclusionCuller hiz_occlusion;
    RuntimeGpuMeshletTablePlan gpu_meshlet_plan;
    RuntimeGpuMeshletTable gpu_meshlet_table;
    RuntimeGpuClusterVisibility gpu_cluster_visibility;
    RuntimeGpuIdentityVisibility gpu_shadow_visibility;
    RuntimeGpuDrawBatcher gpu_draw_batches;
    RuntimeGpuDrawBatcher gpu_pbr_draw_batches;
    RuntimeGpuDrawBatcher gpu_cluster_default_draw_batches;
    RuntimeGpuDrawBatcher gpu_cluster_pbr_draw_batches;
    RuntimeGpuDrawBatcher gpu_shadow_draw_batches;
    RuntimeGpuDrawBatcher gpu_motion_draw_batches;
    RuntimeGpuClusterInstanceRemap gpu_cluster_default_instances;
    RuntimeGpuClusterInstanceRemap gpu_cluster_pbr_instances;
    RuntimeGpuInstanceTable gpu_instances;
    RuntimeGpuInstanceTable gpu_cluster_instances;
    RuntimeGpuInstanceTable gpu_previous_instances;
    RuntimeRayTracingSceneExecutor ray_tracing_scene;
    RuntimeRayTracingExecutionCounters ray_tracing_counters{};
    std::array<RuntimeGpuInstanceTable, runtime_point_shadow_light_count> gpu_point_shadow_instances;
    detail::RuntimeGpuMaterialSceneResources gpu_material_scene;
    std::unique_ptr<render::Buffer> gpu_default_frame;
    std::unique_ptr<render::Buffer> gpu_pbr_frame;
    std::array<std::unique_ptr<render::Buffer>, runtime_shadow_cascade_count> gpu_shadow_frames;
    std::array<
        std::array<std::unique_ptr<render::Buffer>, runtime_point_shadow_face_count>,
        runtime_point_shadow_light_count
    > gpu_point_shadow_frames;
    std::unique_ptr<render::ShaderModule> gpu_pbr_vertex_shader;
    std::unique_ptr<render::ShaderModule> gpu_pbr_fragment_shader;
    std::unique_ptr<render::GraphicsPipeline> gpu_pbr_pipeline;
    std::unique_ptr<render::ShaderModule> vertex_shader;
    std::unique_ptr<render::ShaderModule> fragment_shader;
    std::unique_ptr<render::GraphicsPipeline> pipeline;
    std::unique_ptr<render::ShaderModule> sky_vertex_shader;
    std::unique_ptr<render::ShaderModule> sky_fragment_shader;
    std::unique_ptr<render::GraphicsPipeline> sky_pipeline;
    std::unique_ptr<render::Buffer> sky_constants;
    std::unique_ptr<render::ShaderModule> motion_vertex_shader;
    std::unique_ptr<render::ShaderModule> motion_fragment_shader;
    std::unique_ptr<render::GraphicsPipeline> motion_pipeline;
    std::unique_ptr<render::Buffer> gpu_motion_frame;
    std::unique_ptr<render::ShaderModule> taa_vertex_shader;
    std::unique_ptr<render::ShaderModule> taa_fragment_shader;
    std::unique_ptr<render::GraphicsPipeline> taa_pipeline;
    std::unique_ptr<render::Sampler> taa_linear_sampler;
    std::unique_ptr<render::Sampler> taa_point_sampler;
    std::unique_ptr<render::Buffer> taa_constants;
    std::unique_ptr<render::ShaderModule> bloom_vertex_shader;
    std::unique_ptr<render::ShaderModule> bloom_extract_fragment_shader;
    std::unique_ptr<render::ShaderModule> bloom_blur_fragment_shader;
    std::unique_ptr<render::ShaderModule> bloom_composite_fragment_shader;
    std::unique_ptr<render::GraphicsPipeline> bloom_extract_pipeline;
    std::unique_ptr<render::GraphicsPipeline> bloom_blur_pipeline;
    std::unique_ptr<render::GraphicsPipeline> bloom_composite_pipeline;
    std::unique_ptr<render::Sampler> bloom_sampler;
    std::array<std::unique_ptr<render::Buffer>, 4U> bloom_constants;
    std::unique_ptr<render::ShaderModule> tone_map_vertex_shader;
    std::unique_ptr<render::ShaderModule> tone_map_fragment_shader;
    std::unique_ptr<render::GraphicsPipeline> tone_map_pipeline;
    std::unique_ptr<render::Sampler> tone_map_sampler;
    std::unique_ptr<render::Buffer> tone_map_constants;
    std::unique_ptr<render::ShaderModule> gpu_shadow_vertex_shader;
    std::unique_ptr<render::GraphicsPipeline> gpu_shadow_pipeline;
    std::array<std::unique_ptr<render::Texture>, runtime_shadow_cascade_count> shadow_maps;
    std::array<
        std::array<std::unique_ptr<render::Texture>, runtime_point_shadow_face_count>,
        runtime_point_shadow_light_count
    > point_shadow_maps;
    std::unique_ptr<render::Sampler> shadow_sampler;
    std::unique_ptr<render::Texture> scene_color;
    std::array<std::unique_ptr<render::Texture>, 2U> temporal_color_history;
    std::array<std::unique_ptr<render::Texture>, 2U> depth_history;
    std::unique_ptr<render::Texture> motion_vectors;
    std::unique_ptr<render::Texture> bloom_extract;
    std::unique_ptr<render::Texture> bloom_blur;
    std::unique_ptr<render::Texture> bloom_composite;
    std::unordered_map<world::ResourceId, MeshGpu, world::ResourceIdHash> meshes;
    std::unordered_map<world::PersistentId, std::unique_ptr<render::Buffer>, world::PersistentIdHash> constants;
    std::unordered_map<world::PersistentId, math::Mat4, world::PersistentIdHash> previous_models;
    std::uint32_t depth_width = 0;
    std::uint32_t depth_height = 0;
    std::uint32_t bloom_width = 0;
    std::uint32_t bloom_height = 0;
    RuntimeTemporalHistory temporal_history;
    detail::RuntimeHiZHistory hiz_history;
    RuntimePostProcessPlan post_process_plan = default_runtime_post_process_plan();
    RuntimeRenderResourceBudgets resource_budgets{};
    std::uint64_t gpu_material_texture_generation = 0U;
    std::size_t history_write_index = 0U;
    float exposure = 1.0F;
    float bloom_threshold = 1.0F;
    float bloom_soft_knee = 0.5F;
    float bloom_strength = 0.08F;
    float vignette_intensity = 0.12F;
    bool initialized = false;
};

RuntimeSceneRenderer::RuntimeSceneRenderer() : state_{std::make_unique<State>()} {}
RuntimeSceneRenderer::~RuntimeSceneRenderer() = default;
RuntimeSceneRenderer::RuntimeSceneRenderer(RuntimeSceneRenderer&&) noexcept = default;
RuntimeSceneRenderer& RuntimeSceneRenderer::operator=(RuntimeSceneRenderer&&) noexcept = default;

bool RuntimeSceneRenderer::initialize(
    render::Device& device,
    const render::TextureFormat color_format,
    const std::filesystem::path& content_root,
    std::string* const diagnostics
) {
    if (state_ == nullptr) {
        set_diagnostics(diagnostics, "Runtime scene renderer state is unavailable.");
        return false;
    }
    state_->device = &device;
    if (!state_->catalog.load(content_root)) {
        set_diagnostics(diagnostics, state_->catalog.diagnostics());
        return false;
    }
    auto gpu_meshlet_build = build_runtime_gpu_meshlet_table_plan(state_->catalog.records());
    if (!gpu_meshlet_build) {
        set_diagnostics(diagnostics, gpu_meshlet_build.diagnostics);
        return false;
    }
    state_->gpu_meshlet_plan = std::move(gpu_meshlet_build.plan);
    if (!state_->materials.initialize(
            device,
            render::TextureFormat::rgba16_float,
            mesh_layouts,
            content_root,
            diagnostics
        )) {
        return false;
    }
    auto gpu_material_build = build_runtime_gpu_material_table_plan(
        RuntimeGpuMaterialTableBuildInput{
            .materials = state_->materials.catalog().records(),
            .validate_texture_references = false,
        }
    );
    if (!gpu_material_build) {
        set_diagnostics(diagnostics, gpu_material_build.diagnostics);
        return false;
    }
    if (!state_->gpu_material_scene.initialize(
            device,
            std::move(gpu_material_build.plan),
            state_->materials,
            diagnostics
        )) {
        return false;
    }
    if (!state_->environment.initialize(device, content_root, diagnostics)) {
        return false;
    }
    if (!state_->visibility.initialize(device, diagnostics) ||
        !state_->hiz_pyramid.initialize(device, diagnostics) ||
        !state_->hiz_occlusion.initialize(device, diagnostics) ||
        !state_->gpu_meshlet_table.initialize(device, diagnostics) ||
        !state_->gpu_cluster_visibility.initialize(device, diagnostics) ||
        !state_->gpu_shadow_visibility.initialize(device, diagnostics) ||
        !state_->gpu_draw_batches.initialize(device, diagnostics) ||
        !state_->gpu_pbr_draw_batches.initialize(device, diagnostics) ||
        !state_->gpu_cluster_default_draw_batches.initialize(device, diagnostics) ||
        !state_->gpu_cluster_pbr_draw_batches.initialize(device, diagnostics) ||
        !state_->gpu_shadow_draw_batches.initialize(device, diagnostics) ||
        !state_->gpu_motion_draw_batches.initialize(device, diagnostics) ||
        !state_->gpu_cluster_default_instances.initialize(device, diagnostics) ||
        !state_->gpu_cluster_pbr_instances.initialize(device, diagnostics) ||
        !state_->gpu_instances.initialize(device, diagnostics) ||
        !state_->gpu_cluster_instances.initialize(device, diagnostics) ||
        !state_->gpu_previous_instances.initialize(device, diagnostics)) {
        return false;
    }
    for (auto& table : state_->gpu_point_shadow_instances) {
        if (!table.initialize(device, diagnostics)) return false;
    }
    auto meshlet_commands = device.create_command_list();
    if (!meshlet_commands || !state_->gpu_meshlet_table.upload(
            *meshlet_commands, state_->gpu_meshlet_plan, diagnostics
        )) {
        set_diagnostics(diagnostics, "VICE Runtime could not upload immutable GPU meshlet metadata.");
        return false;
    }
    const auto meshlet_submission = device.submit(std::move(meshlet_commands));
    if (meshlet_submission == render::invalid_submission ||
        !device.wait_for_submission(meshlet_submission)) {
        set_diagnostics(diagnostics, "VICE Runtime GPU meshlet metadata submission failed.");
        return false;
    }
    state_->gpu_default_frame = create_gpu_default_frame_buffer(device);
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
    for (auto& light_frames : state_->gpu_point_shadow_frames) {
        for (auto& frame : light_frames) {
            frame = create_gpu_shadow_frame_buffer(device);
            if (!frame) {
                set_diagnostics(diagnostics, "VICE Runtime could not create GPU point-shadow frame constants.");
                return false;
            }
        }
    }

    const bool metal = device.backend() == render::Backend::metal;
    render::ShaderModuleDesc vertex_desc;
    vertex_desc.stage = render::ShaderStage::vertex;
    vertex_desc.language = metal ? render::ShaderSourceLanguage::metal : render::ShaderSourceLanguage::hlsl;
    vertex_desc.source = metal ? metal_source : hlsl_source;
    vertex_desc.resources = vertex_resources;
    vertex_desc.entry_point = "vice_runtime_mesh_vs";
    vertex_desc.debug_name = "VICE Runtime mesh vertex fallback";

    render::ShaderModuleDesc fragment_desc;
    fragment_desc.stage = render::ShaderStage::fragment;
    fragment_desc.language = vertex_desc.language;
    fragment_desc.source = vertex_desc.source;
    fragment_desc.entry_point = "vice_runtime_mesh_fs";
    fragment_desc.debug_name = "VICE Runtime mesh fragment fallback";

    state_->vertex_shader = device.create_shader_module(vertex_desc);
    state_->fragment_shader = device.create_shader_module(fragment_desc);
    if (!state_->vertex_shader || !state_->fragment_shader) {
        set_diagnostics(diagnostics, "VICE Runtime could not create fallback scene shaders.");
        return false;
    }

    render::GraphicsPipelineDesc pipeline_desc;
    pipeline_desc.vertex_shader = state_->vertex_shader.get();
    pipeline_desc.fragment_shader = state_->fragment_shader.get();
    pipeline_desc.color_format = render::TextureFormat::rgba16_float;
    pipeline_desc.cull_mode = render::CullMode::none;
    pipeline_desc.depth.format = render::TextureFormat::depth32_float;
    pipeline_desc.depth.test_enabled = true;
    pipeline_desc.depth.write_enabled = true;
    pipeline_desc.depth.compare = render::CompareOp::less_equal;
    pipeline_desc.vertex_buffers = mesh_layouts;
    pipeline_desc.debug_name = "VICE Runtime mesh fallback pipeline";
    state_->pipeline = device.create_graphics_pipeline(pipeline_desc);
    if (!state_->pipeline) {
        set_diagnostics(diagnostics, "VICE Runtime could not create the fallback scene graphics pipeline.");
        return false;
    }

    const auto material_records = state_->materials.catalog().records();
    const auto gpu_pbr_material = std::find_if(
        material_records.begin(),
        material_records.end(),
        [](const CookedRenderMaterialRecord& record) {
            return render::render_material_uses_gpu_scene_pbr(record.asset.version);
        }
    );
    if (gpu_pbr_material != material_records.end()) {
        const auto vertex_manifest = (
            gpu_pbr_material->path.parent_path() /
            gpu_pbr_material->asset.vertex_shader_manifest
        ).lexically_normal();
        const auto fragment_manifest = (
            gpu_pbr_material->path.parent_path() /
            gpu_pbr_material->asset.fragment_shader_manifest
        ).lexically_normal();
        auto gpu_pbr_vertex = render::load_cooked_shader(device, vertex_manifest);
        auto gpu_pbr_fragment = render::load_cooked_shader(device, fragment_manifest);
        if (!gpu_pbr_vertex || !gpu_pbr_fragment ||
            gpu_pbr_vertex.module->stage() != render::ShaderStage::vertex ||
            gpu_pbr_fragment.module->stage() != render::ShaderStage::fragment) {
            set_diagnostics(
                diagnostics,
                "VICE Runtime could not load the standard GPU PBR shader pair."
            );
            return false;
        }
        state_->gpu_pbr_vertex_shader = std::move(gpu_pbr_vertex.module);
        state_->gpu_pbr_fragment_shader = std::move(gpu_pbr_fragment.module);

        render::GraphicsPipelineDesc gpu_pbr_pipeline_desc;
        gpu_pbr_pipeline_desc.vertex_shader = state_->gpu_pbr_vertex_shader.get();
        gpu_pbr_pipeline_desc.fragment_shader = state_->gpu_pbr_fragment_shader.get();
        gpu_pbr_pipeline_desc.color_format = render::TextureFormat::rgba16_float;
        gpu_pbr_pipeline_desc.cull_mode = render::CullMode::none;
        gpu_pbr_pipeline_desc.depth.format = render::TextureFormat::depth32_float;
        gpu_pbr_pipeline_desc.depth.test_enabled = true;
        gpu_pbr_pipeline_desc.depth.write_enabled = true;
        gpu_pbr_pipeline_desc.depth.compare = render::CompareOp::less_equal;
        gpu_pbr_pipeline_desc.vertex_buffers = mesh_layouts;
        gpu_pbr_pipeline_desc.debug_name = "VICE Runtime indexed GPU PBR pipeline";
        state_->gpu_pbr_pipeline = device.create_graphics_pipeline(gpu_pbr_pipeline_desc);
        state_->gpu_pbr_frame = create_gpu_pbr_frame_buffer(device);
        if (!state_->gpu_pbr_pipeline || !state_->gpu_pbr_frame) {
            set_diagnostics(diagnostics, "VICE Runtime could not create indexed GPU PBR resources.");
            return false;
        }
    }

    render::ShaderModuleDesc sky_vertex_desc;
    sky_vertex_desc.stage = render::ShaderStage::vertex;
    sky_vertex_desc.language = vertex_desc.language;
    sky_vertex_desc.source = metal ? detail::sky_metal_source : detail::sky_hlsl_source;
    sky_vertex_desc.entry_point = "vice_runtime_sky_vs";
    sky_vertex_desc.debug_name = "VICE Runtime sky vertex";

    render::ShaderModuleDesc sky_fragment_desc;
    sky_fragment_desc.stage = render::ShaderStage::fragment;
    sky_fragment_desc.language = vertex_desc.language;
    sky_fragment_desc.source = sky_vertex_desc.source;
    sky_fragment_desc.resources = detail::sky_fragment_resources;
    sky_fragment_desc.entry_point = "vice_runtime_sky_fs";
    sky_fragment_desc.debug_name = "VICE Runtime sky fragment";

    state_->sky_vertex_shader = device.create_shader_module(sky_vertex_desc);
    state_->sky_fragment_shader = device.create_shader_module(sky_fragment_desc);
    if (!state_->sky_vertex_shader || !state_->sky_fragment_shader) {
        set_diagnostics(diagnostics, "VICE Runtime could not create sky/atmosphere shaders.");
        return false;
    }

    render::GraphicsPipelineDesc sky_pipeline_desc;
    sky_pipeline_desc.vertex_shader = state_->sky_vertex_shader.get();
    sky_pipeline_desc.fragment_shader = state_->sky_fragment_shader.get();
    sky_pipeline_desc.color_format = render::TextureFormat::rgba16_float;
    sky_pipeline_desc.cull_mode = render::CullMode::none;
    sky_pipeline_desc.debug_name = "VICE Runtime sky/atmosphere pipeline";
    state_->sky_pipeline = device.create_graphics_pipeline(sky_pipeline_desc);
    state_->sky_constants = create_sky_constant_buffer(device);
    if (!state_->sky_pipeline || !state_->sky_constants) {
        set_diagnostics(diagnostics, "VICE Runtime could not create sky/atmosphere resources.");
        return false;
    }

    render::ShaderModuleDesc motion_vertex_desc;
    motion_vertex_desc.stage = render::ShaderStage::vertex;
    motion_vertex_desc.language = vertex_desc.language;
    motion_vertex_desc.source = metal ? motion_metal_source : motion_hlsl_source;
    motion_vertex_desc.resources = motion_vertex_resources;
    motion_vertex_desc.entry_point = "vice_runtime_motion_vs";
    motion_vertex_desc.debug_name = "VICE Runtime motion-vector vertex";

    render::ShaderModuleDesc motion_fragment_desc;
    motion_fragment_desc.stage = render::ShaderStage::fragment;
    motion_fragment_desc.language = vertex_desc.language;
    motion_fragment_desc.source = motion_vertex_desc.source;
    motion_fragment_desc.entry_point = "vice_runtime_motion_fs";
    motion_fragment_desc.debug_name = "VICE Runtime motion-vector fragment";

    state_->motion_vertex_shader = device.create_shader_module(motion_vertex_desc);
    state_->motion_fragment_shader = device.create_shader_module(motion_fragment_desc);
    if (!state_->motion_vertex_shader || !state_->motion_fragment_shader) {
        set_diagnostics(diagnostics, "VICE Runtime could not create motion-vector shaders.");
        return false;
    }

    render::GraphicsPipelineDesc motion_pipeline_desc;
    motion_pipeline_desc.vertex_shader = state_->motion_vertex_shader.get();
    motion_pipeline_desc.fragment_shader = state_->motion_fragment_shader.get();
    motion_pipeline_desc.color_format = render::TextureFormat::rgba16_float;
    motion_pipeline_desc.cull_mode = render::CullMode::none;
    motion_pipeline_desc.depth.format = render::TextureFormat::depth32_float;
    motion_pipeline_desc.depth.test_enabled = true;
    motion_pipeline_desc.depth.write_enabled = false;
    motion_pipeline_desc.depth.compare = render::CompareOp::less_equal;
    motion_pipeline_desc.vertex_buffers = mesh_layouts;
    motion_pipeline_desc.debug_name = "VICE Runtime motion-vector pipeline";
    state_->motion_pipeline = device.create_graphics_pipeline(motion_pipeline_desc);
    state_->gpu_motion_frame = create_motion_constant_buffer(device);
    if (!state_->motion_pipeline || !state_->gpu_motion_frame) {
        set_diagnostics(diagnostics, "VICE Runtime could not create the motion-vector pipeline/resources.");
        return false;
    }

    render::ShaderModuleDesc taa_vertex_desc;
    taa_vertex_desc.stage = render::ShaderStage::vertex;
    taa_vertex_desc.language = vertex_desc.language;
    taa_vertex_desc.source = metal ? detail::taa_metal_source : detail::taa_hlsl_source;
    taa_vertex_desc.entry_point = "vice_runtime_taa_vs";
    taa_vertex_desc.debug_name = "VICE Runtime TAA vertex";

    render::ShaderModuleDesc taa_fragment_desc;
    taa_fragment_desc.stage = render::ShaderStage::fragment;
    taa_fragment_desc.language = vertex_desc.language;
    taa_fragment_desc.source = taa_vertex_desc.source;
    taa_fragment_desc.resources = detail::taa_fragment_resources;
    taa_fragment_desc.entry_point = "vice_runtime_taa_fs";
    taa_fragment_desc.debug_name = "VICE Runtime TAA fragment";

    state_->taa_vertex_shader = device.create_shader_module(taa_vertex_desc);
    state_->taa_fragment_shader = device.create_shader_module(taa_fragment_desc);
    if (!state_->taa_vertex_shader || !state_->taa_fragment_shader) {
        set_diagnostics(diagnostics, "VICE Runtime could not create TAA shaders.");
        return false;
    }

    render::GraphicsPipelineDesc taa_pipeline_desc;
    taa_pipeline_desc.vertex_shader = state_->taa_vertex_shader.get();
    taa_pipeline_desc.fragment_shader = state_->taa_fragment_shader.get();
    taa_pipeline_desc.color_format = render::TextureFormat::rgba16_float;
    taa_pipeline_desc.cull_mode = render::CullMode::none;
    taa_pipeline_desc.debug_name = "VICE Runtime TAA pipeline";
    state_->taa_pipeline = device.create_graphics_pipeline(taa_pipeline_desc);
    if (!state_->taa_pipeline) {
        set_diagnostics(diagnostics, "VICE Runtime could not create the TAA pipeline.");
        return false;
    }

    render::SamplerDesc taa_linear_sampler_desc;
    taa_linear_sampler_desc.min_filter = render::Filter::linear;
    taa_linear_sampler_desc.mag_filter = render::Filter::linear;
    taa_linear_sampler_desc.mip_filter = render::Filter::linear;
    taa_linear_sampler_desc.address_u = render::AddressMode::clamp_to_edge;
    taa_linear_sampler_desc.address_v = render::AddressMode::clamp_to_edge;
    taa_linear_sampler_desc.address_w = render::AddressMode::clamp_to_edge;
    taa_linear_sampler_desc.debug_name = "VICE Runtime TAA linear sampler";
    state_->taa_linear_sampler = device.create_sampler(taa_linear_sampler_desc);

    render::SamplerDesc taa_point_sampler_desc = taa_linear_sampler_desc;
    taa_point_sampler_desc.min_filter = render::Filter::nearest;
    taa_point_sampler_desc.mag_filter = render::Filter::nearest;
    taa_point_sampler_desc.mip_filter = render::Filter::nearest;
    taa_point_sampler_desc.debug_name = "VICE Runtime TAA point sampler";
    state_->taa_point_sampler = device.create_sampler(taa_point_sampler_desc);
    state_->taa_constants = create_taa_constant_buffer(device);
    if (!state_->taa_linear_sampler || !state_->taa_point_sampler || !state_->taa_constants) {
        set_diagnostics(diagnostics, "VICE Runtime could not create TAA resources.");
        return false;
    }

    render::ShaderModuleDesc bloom_vertex_desc;
    bloom_vertex_desc.stage = render::ShaderStage::vertex;
    bloom_vertex_desc.language = vertex_desc.language;
    bloom_vertex_desc.source = metal ? detail::bloom_metal_source : detail::bloom_hlsl_source;
    bloom_vertex_desc.entry_point = "vice_runtime_bloom_vs";
    bloom_vertex_desc.debug_name = "VICE Runtime bloom vertex";

    render::ShaderModuleDesc bloom_extract_fragment_desc;
    bloom_extract_fragment_desc.stage = render::ShaderStage::fragment;
    bloom_extract_fragment_desc.language = vertex_desc.language;
    bloom_extract_fragment_desc.source = bloom_vertex_desc.source;
    bloom_extract_fragment_desc.resources = detail::bloom_single_fragment_resources;
    bloom_extract_fragment_desc.entry_point = "vice_runtime_bloom_extract_fs";
    bloom_extract_fragment_desc.debug_name = "VICE Runtime bloom extract fragment";

    render::ShaderModuleDesc bloom_blur_fragment_desc = bloom_extract_fragment_desc;
    bloom_blur_fragment_desc.entry_point = "vice_runtime_bloom_blur_fs";
    bloom_blur_fragment_desc.debug_name = "VICE Runtime bloom blur fragment";

    render::ShaderModuleDesc bloom_composite_fragment_desc = bloom_extract_fragment_desc;
    bloom_composite_fragment_desc.resources = detail::bloom_composite_fragment_resources;
    bloom_composite_fragment_desc.entry_point = "vice_runtime_bloom_composite_fs";
    bloom_composite_fragment_desc.debug_name = "VICE Runtime bloom composite fragment";

    state_->bloom_vertex_shader = device.create_shader_module(bloom_vertex_desc);
    state_->bloom_extract_fragment_shader = device.create_shader_module(bloom_extract_fragment_desc);
    state_->bloom_blur_fragment_shader = device.create_shader_module(bloom_blur_fragment_desc);
    state_->bloom_composite_fragment_shader = device.create_shader_module(bloom_composite_fragment_desc);
    if (!state_->bloom_vertex_shader || !state_->bloom_extract_fragment_shader ||
        !state_->bloom_blur_fragment_shader || !state_->bloom_composite_fragment_shader) {
        set_diagnostics(diagnostics, "VICE Runtime could not create bloom shaders.");
        return false;
    }

    render::GraphicsPipelineDesc bloom_pipeline_desc;
    bloom_pipeline_desc.vertex_shader = state_->bloom_vertex_shader.get();
    bloom_pipeline_desc.fragment_shader = state_->bloom_extract_fragment_shader.get();
    bloom_pipeline_desc.color_format = render::TextureFormat::rgba16_float;
    bloom_pipeline_desc.cull_mode = render::CullMode::none;
    bloom_pipeline_desc.debug_name = "VICE Runtime bloom extract pipeline";
    state_->bloom_extract_pipeline = device.create_graphics_pipeline(bloom_pipeline_desc);

    bloom_pipeline_desc.fragment_shader = state_->bloom_blur_fragment_shader.get();
    bloom_pipeline_desc.debug_name = "VICE Runtime bloom blur pipeline";
    state_->bloom_blur_pipeline = device.create_graphics_pipeline(bloom_pipeline_desc);

    bloom_pipeline_desc.fragment_shader = state_->bloom_composite_fragment_shader.get();
    bloom_pipeline_desc.debug_name = "VICE Runtime bloom composite pipeline";
    state_->bloom_composite_pipeline = device.create_graphics_pipeline(bloom_pipeline_desc);
    if (!state_->bloom_extract_pipeline || !state_->bloom_blur_pipeline ||
        !state_->bloom_composite_pipeline) {
        set_diagnostics(diagnostics, "VICE Runtime could not create bloom pipelines.");
        return false;
    }

    render::SamplerDesc bloom_sampler_desc;
    bloom_sampler_desc.min_filter = render::Filter::linear;
    bloom_sampler_desc.mag_filter = render::Filter::linear;
    bloom_sampler_desc.mip_filter = render::Filter::linear;
    bloom_sampler_desc.address_u = render::AddressMode::clamp_to_edge;
    bloom_sampler_desc.address_v = render::AddressMode::clamp_to_edge;
    bloom_sampler_desc.address_w = render::AddressMode::clamp_to_edge;
    bloom_sampler_desc.debug_name = "VICE Runtime bloom sampler";
    state_->bloom_sampler = device.create_sampler(bloom_sampler_desc);
    if (!state_->bloom_sampler) {
        set_diagnostics(diagnostics, "VICE Runtime could not create the bloom sampler.");
        return false;
    }
    for (auto& constants : state_->bloom_constants) {
        constants = create_bloom_constant_buffer(device);
        if (!constants) {
            set_diagnostics(diagnostics, "VICE Runtime could not create bloom constants.");
            return false;
        }
    }
    if (!write_bloom_constants(*state_->bloom_constants[0], 1U, 1U,
            state_->bloom_threshold, state_->bloom_soft_knee, state_->bloom_strength, 0.0F, 0.0F) ||
        !write_bloom_constants(*state_->bloom_constants[1], 1U, 1U,
            state_->bloom_threshold, state_->bloom_soft_knee, state_->bloom_strength, 1.0F, 0.0F) ||
        !write_bloom_constants(*state_->bloom_constants[2], 1U, 1U,
            state_->bloom_threshold, state_->bloom_soft_knee, state_->bloom_strength, 0.0F, 1.0F) ||
        !write_bloom_constants(*state_->bloom_constants[3], 1U, 1U,
            state_->bloom_threshold, state_->bloom_soft_knee, state_->bloom_strength, 0.0F, 0.0F)) {
        set_diagnostics(diagnostics, "VICE Runtime could not initialize bloom constants.");
        return false;
    }

    render::ShaderModuleDesc tone_vertex_desc;
    tone_vertex_desc.stage = render::ShaderStage::vertex;
    tone_vertex_desc.language = vertex_desc.language;
    tone_vertex_desc.source = metal ? tone_map_metal_source : tone_map_hlsl_source;
    tone_vertex_desc.entry_point = "vice_runtime_tonemap_vs";
    tone_vertex_desc.debug_name = "VICE Runtime tone-map vertex";

    render::ShaderModuleDesc tone_fragment_desc;
    tone_fragment_desc.stage = render::ShaderStage::fragment;
    tone_fragment_desc.language = vertex_desc.language;
    tone_fragment_desc.source = tone_vertex_desc.source;
    tone_fragment_desc.resources = tone_map_fragment_resources;
    tone_fragment_desc.entry_point = "vice_runtime_tonemap_fs";
    tone_fragment_desc.debug_name = "VICE Runtime tone-map fragment";

    state_->tone_map_vertex_shader = device.create_shader_module(tone_vertex_desc);
    state_->tone_map_fragment_shader = device.create_shader_module(tone_fragment_desc);
    if (!state_->tone_map_vertex_shader || !state_->tone_map_fragment_shader) {
        set_diagnostics(diagnostics, "VICE Runtime could not create tone-map shaders.");
        return false;
    }

    render::GraphicsPipelineDesc tone_pipeline_desc;
    tone_pipeline_desc.vertex_shader = state_->tone_map_vertex_shader.get();
    tone_pipeline_desc.fragment_shader = state_->tone_map_fragment_shader.get();
    tone_pipeline_desc.color_format = color_format;
    tone_pipeline_desc.cull_mode = render::CullMode::none;
    tone_pipeline_desc.debug_name = "VICE Runtime tone-map pipeline";
    state_->tone_map_pipeline = device.create_graphics_pipeline(tone_pipeline_desc);
    if (!state_->tone_map_pipeline) {
        set_diagnostics(diagnostics, "VICE Runtime could not create the tone-map pipeline.");
        return false;
    }

    render::SamplerDesc tone_sampler_desc;
    tone_sampler_desc.min_filter = render::Filter::linear;
    tone_sampler_desc.mag_filter = render::Filter::linear;
    tone_sampler_desc.mip_filter = render::Filter::linear;
    tone_sampler_desc.address_u = render::AddressMode::clamp_to_edge;
    tone_sampler_desc.address_v = render::AddressMode::clamp_to_edge;
    tone_sampler_desc.address_w = render::AddressMode::clamp_to_edge;
    tone_sampler_desc.debug_name = "VICE Runtime tone-map sampler";
    state_->tone_map_sampler = device.create_sampler(tone_sampler_desc);
    state_->tone_map_constants = create_tone_map_constant_buffer(device);
    if (!state_->tone_map_sampler || !state_->tone_map_constants) {
        set_diagnostics(diagnostics, "VICE Runtime could not create tone-map resources.");
        return false;
    }
    const ToneMapConstants default_tone_map_constants{
        .exposure = state_->exposure,
        .vignette_intensity = state_->vignette_intensity,
    };
    if (!state_->tone_map_constants->write(
            0U,
            std::as_bytes(std::span{&default_tone_map_constants, 1})
        )) {
        set_diagnostics(diagnostics, "VICE Runtime could not initialize tone-map exposure.");
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
    if (!state_->gpu_shadow_pipeline) {
        set_diagnostics(diagnostics, "VICE Runtime could not create the GPU directional shadow pipeline.");
        return false;
    }

    render::TextureDesc shadow_desc;
    shadow_desc.width = runtime_shadow_map_size;
    shadow_desc.height = runtime_shadow_map_size;
    shadow_desc.format = render::TextureFormat::depth32_float;
    shadow_desc.usage = render::TextureUsage::depth_attachment | render::TextureUsage::sampled;
    shadow_desc.debug_name = "VICE Runtime directional shadow cascade";
    for (auto& shadow_map : state_->shadow_maps) {
        shadow_map = device.create_texture(shadow_desc);
        if (!shadow_map) {
            set_diagnostics(diagnostics, "VICE Runtime could not create a directional shadow cascade.");
            return false;
        }
    }

    render::TextureDesc point_shadow_desc = shadow_desc;
    point_shadow_desc.width = runtime_point_shadow_map_size;
    point_shadow_desc.height = runtime_point_shadow_map_size;
    point_shadow_desc.debug_name = "VICE Runtime point-light shadow face";
    for (auto& point_light_maps : state_->point_shadow_maps) {
        for (auto& shadow_face : point_light_maps) {
            shadow_face = device.create_texture(point_shadow_desc);
            if (!shadow_face) {
                set_diagnostics(diagnostics, "VICE Runtime could not create a point-light shadow face.");
                return false;
            }
        }
    }

    render::SamplerDesc shadow_sampler_desc;
    shadow_sampler_desc.min_filter = render::Filter::nearest;
    shadow_sampler_desc.mag_filter = render::Filter::nearest;
    shadow_sampler_desc.mip_filter = render::Filter::nearest;
    shadow_sampler_desc.address_u = render::AddressMode::clamp_to_edge;
    shadow_sampler_desc.address_v = render::AddressMode::clamp_to_edge;
    shadow_sampler_desc.address_w = render::AddressMode::clamp_to_edge;
    shadow_sampler_desc.debug_name = "VICE Runtime directional shadow sampler";
    state_->shadow_sampler = device.create_sampler(shadow_sampler_desc);
    if (!state_->shadow_sampler) {
        set_diagnostics(diagnostics, "VICE Runtime could not create the directional shadow sampler.");
        return false;
    }


    state_->initialized = true;
    if (diagnostics != nullptr) diagnostics->clear();
    return true;
}

bool RuntimeSceneRenderer::resize(
    const platform::WindowMetrics metrics,
    std::string* const diagnostics
) {
    if (state_ == nullptr || !state_->initialized || state_->device == nullptr) {
        set_diagnostics(diagnostics, "Runtime scene renderer is not initialized.");
        return false;
    }
    if (metrics.drawable_width == 0U || metrics.drawable_height == 0U) {
        state_->scene_color.reset();
        for (auto& color : state_->temporal_color_history) color.reset();
        for (auto& depth : state_->depth_history) depth.reset();
        state_->motion_vectors.reset();
        state_->bloom_extract.reset();
        state_->bloom_blur.reset();
        state_->bloom_composite.reset();
        state_->previous_models.clear();
        state_->temporal_history.reset();
        state_->hiz_history.reset();
        state_->history_write_index = 0U;
        state_->depth_width = 0;
        state_->depth_height = 0;
        state_->bloom_width = 0;
        state_->bloom_height = 0;
        if (diagnostics != nullptr) diagnostics->clear();
        return true;
    }
    if (state_->scene_color && state_->temporal_color_history[0] &&
        state_->temporal_color_history[1] && state_->depth_history[0] &&
        state_->depth_history[1] && state_->motion_vectors && state_->bloom_extract &&
        state_->bloom_blur && state_->bloom_composite &&
        state_->depth_width == metrics.drawable_width &&
        state_->depth_height == metrics.drawable_height) {
        if (diagnostics != nullptr) diagnostics->clear();
        return true;
    }

    render::TextureDesc depth_desc;
    depth_desc.width = metrics.drawable_width;
    depth_desc.height = metrics.drawable_height;
    depth_desc.format = render::TextureFormat::depth32_float;
    depth_desc.usage = render::TextureUsage::depth_attachment | render::TextureUsage::sampled;
    depth_desc.debug_name = "VICE Runtime TAA depth history";
    std::array<std::unique_ptr<render::Texture>, 2U> depth_history;
    for (auto& depth : depth_history) {
        depth = state_->device->create_texture(depth_desc);
        if (!depth) {
            set_diagnostics(diagnostics, "VICE Runtime could not create TAA depth-history targets.");
            return false;
        }
    }

    render::TextureDesc scene_color_desc;
    scene_color_desc.width = metrics.drawable_width;
    scene_color_desc.height = metrics.drawable_height;
    scene_color_desc.format = render::TextureFormat::rgba16_float;
    scene_color_desc.usage = render::TextureUsage::color_attachment | render::TextureUsage::sampled;
    scene_color_desc.debug_name = "VICE Runtime raw HDR scene color";
    auto scene_color = state_->device->create_texture(scene_color_desc);
    if (!scene_color) {
        set_diagnostics(diagnostics, "VICE Runtime could not create the raw HDR scene target.");
        return false;
    }

    scene_color_desc.debug_name = "VICE Runtime TAA color history";
    std::array<std::unique_ptr<render::Texture>, 2U> temporal_color_history;
    for (auto& history_color : temporal_color_history) {
        history_color = state_->device->create_texture(scene_color_desc);
        if (!history_color) {
            set_diagnostics(diagnostics, "VICE Runtime could not create TAA color-history targets.");
            return false;
        }
    }

    render::TextureDesc motion_desc;
    motion_desc.width = metrics.drawable_width;
    motion_desc.height = metrics.drawable_height;
    motion_desc.format = render::TextureFormat::rgba16_float;
    motion_desc.usage = render::TextureUsage::color_attachment | render::TextureUsage::sampled;
    motion_desc.debug_name = "VICE Runtime motion vectors";
    auto motion_vectors = state_->device->create_texture(motion_desc);
    if (!motion_vectors) {
        set_diagnostics(diagnostics, "VICE Runtime could not create the motion-vector target.");
        return false;
    }

    const std::uint32_t bloom_width = std::max(1U, (metrics.drawable_width + 1U) / 2U);
    const std::uint32_t bloom_height = std::max(1U, (metrics.drawable_height + 1U) / 2U);
    render::TextureDesc bloom_desc;
    bloom_desc.width = bloom_width;
    bloom_desc.height = bloom_height;
    bloom_desc.format = render::TextureFormat::rgba16_float;
    bloom_desc.usage = render::TextureUsage::color_attachment | render::TextureUsage::sampled;
    bloom_desc.debug_name = "VICE Runtime bloom extract";
    auto bloom_extract = state_->device->create_texture(bloom_desc);
    bloom_desc.debug_name = "VICE Runtime bloom blur";
    auto bloom_blur = state_->device->create_texture(bloom_desc);

    render::TextureDesc bloom_composite_desc = scene_color_desc;
    bloom_composite_desc.debug_name = "VICE Runtime bloom composite";
    auto bloom_composite = state_->device->create_texture(bloom_composite_desc);
    if (!bloom_extract || !bloom_blur || !bloom_composite) {
        set_diagnostics(diagnostics, "VICE Runtime could not create bloom frame targets.");
        return false;
    }

    if (!write_bloom_constants(*state_->bloom_constants[0], bloom_width, bloom_height,
            state_->bloom_threshold, state_->bloom_soft_knee, state_->bloom_strength, 0.0F, 0.0F) ||
        !write_bloom_constants(*state_->bloom_constants[1], bloom_width, bloom_height,
            state_->bloom_threshold, state_->bloom_soft_knee, state_->bloom_strength, 1.0F, 0.0F) ||
        !write_bloom_constants(*state_->bloom_constants[2], bloom_width, bloom_height,
            state_->bloom_threshold, state_->bloom_soft_knee, state_->bloom_strength, 0.0F, 1.0F) ||
        !write_bloom_constants(*state_->bloom_constants[3], bloom_width, bloom_height,
            state_->bloom_threshold, state_->bloom_soft_knee, state_->bloom_strength, 0.0F, 0.0F)) {
        set_diagnostics(diagnostics, "VICE Runtime could not update bloom constants for the drawable size.");
        return false;
    }

    if (!state_->hiz_pyramid.resize(
            metrics.drawable_width,
            metrics.drawable_height,
            diagnostics
        )) {
        return false;
    }

    state_->scene_color = std::move(scene_color);
    state_->temporal_color_history = std::move(temporal_color_history);
    state_->depth_history = std::move(depth_history);
    state_->motion_vectors = std::move(motion_vectors);
    state_->bloom_extract = std::move(bloom_extract);
    state_->bloom_blur = std::move(bloom_blur);
    state_->bloom_composite = std::move(bloom_composite);
    state_->previous_models.clear();
    state_->temporal_history.reset();
    state_->hiz_history.reset();
    state_->history_write_index = 0U;
    state_->depth_width = metrics.drawable_width;
    state_->depth_height = metrics.drawable_height;
    state_->bloom_width = bloom_width;
    state_->bloom_height = bloom_height;
    if (diagnostics != nullptr) diagnostics->clear();
    return true;
}

bool RuntimeSceneRenderer::set_exposure(
    const float exposure,
    std::string* const diagnostics
) {
    if (state_ == nullptr || !state_->initialized || state_->tone_map_constants == nullptr) {
        set_diagnostics(diagnostics, "Runtime scene renderer is not initialized.");
        return false;
    }
    if (!std::isfinite(exposure) || exposure <= 0.0F) {
        set_diagnostics(diagnostics, "Runtime exposure must be finite and greater than zero.");
        return false;
    }
    const ToneMapConstants constants{
        .exposure = exposure,
        .vignette_intensity = state_->vignette_intensity,
    };
    if (!state_->tone_map_constants->write(0U, std::as_bytes(std::span{&constants, 1}))) {
        set_diagnostics(diagnostics, "VICE Runtime could not update tone-map exposure.");
        return false;
    }
    state_->exposure = exposure;
    if (diagnostics != nullptr) diagnostics->clear();
    return true;
}


bool RuntimeSceneRenderer::set_bloom(
    const float threshold,
    const float soft_knee,
    const float strength,
    std::string* const diagnostics
) {
    if (state_ == nullptr || !state_->initialized ||
        !state_->bloom_constants[0] || !state_->bloom_constants[1] ||
        !state_->bloom_constants[2] || !state_->bloom_constants[3]) {
        set_diagnostics(diagnostics, "Runtime scene renderer is not initialized.");
        return false;
    }
    if (!std::isfinite(threshold) || threshold < 0.0F ||
        !std::isfinite(soft_knee) || soft_knee < 0.0F || soft_knee > 1.0F ||
        !std::isfinite(strength) || strength < 0.0F) {
        set_diagnostics(
            diagnostics,
            "Runtime bloom requires a non-negative finite threshold/strength and soft knee in [0, 1]."
        );
        return false;
    }

    const std::uint32_t width = std::max(1U, state_->bloom_width);
    const std::uint32_t height = std::max(1U, state_->bloom_height);
    if (!write_bloom_constants(*state_->bloom_constants[0], width, height,
            threshold, soft_knee, strength, 0.0F, 0.0F) ||
        !write_bloom_constants(*state_->bloom_constants[1], width, height,
            threshold, soft_knee, strength, 1.0F, 0.0F) ||
        !write_bloom_constants(*state_->bloom_constants[2], width, height,
            threshold, soft_knee, strength, 0.0F, 1.0F) ||
        !write_bloom_constants(*state_->bloom_constants[3], width, height,
            threshold, soft_knee, strength, 0.0F, 0.0F)) {
        set_diagnostics(diagnostics, "VICE Runtime could not update bloom parameters.");
        return false;
    }

    state_->bloom_threshold = threshold;
    state_->bloom_soft_knee = soft_knee;
    state_->bloom_strength = strength;
    if (diagnostics != nullptr) diagnostics->clear();
    return true;
}

bool RuntimeSceneRenderer::set_vignette(
    const float intensity,
    std::string* const diagnostics
) {
    if (state_ == nullptr || !state_->initialized || state_->tone_map_constants == nullptr) {
        set_diagnostics(diagnostics, "Runtime scene renderer is not initialized.");
        return false;
    }
    if (!std::isfinite(intensity) || intensity < 0.0F || intensity > 1.0F) {
        set_diagnostics(diagnostics, "Runtime vignette intensity must be finite and in [0, 1].");
        return false;
    }
    const ToneMapConstants constants{
        .exposure = state_->exposure,
        .vignette_intensity = intensity,
    };
    if (!state_->tone_map_constants->write(0U, std::as_bytes(std::span{&constants, 1}))) {
        set_diagnostics(diagnostics, "VICE Runtime could not update vignette intensity.");
        return false;
    }
    state_->vignette_intensity = intensity;
    if (diagnostics != nullptr) diagnostics->clear();
    return true;
}

bool RuntimeSceneRenderer::set_resource_budgets(
    const RuntimeRenderResourceBudgets budgets,
    std::string* const diagnostics
) {
    if (state_ == nullptr || !state_->initialized) {
        set_diagnostics(diagnostics, "Runtime scene renderer is not initialized.");
        return false;
    }
    state_->resource_budgets = budgets;
    if (diagnostics != nullptr) diagnostics->clear();
    return true;
}

bool RuntimeSceneRenderer::render(
    const world::World& world,
    render::Swapchain& swapchain,
    const platform::WindowMetrics metrics,
    const render::Color clear_color,
    std::string* const diagnostics
) {
    if (state_ == nullptr || !state_->initialized || state_->device == nullptr ||
        !state_->pipeline || !state_->sky_pipeline || !state_->sky_constants ||
        !state_->motion_pipeline || !state_->taa_pipeline ||
        !state_->taa_linear_sampler || !state_->taa_point_sampler || !state_->taa_constants ||
        !state_->bloom_extract_pipeline || !state_->bloom_blur_pipeline ||
        !state_->bloom_composite_pipeline || !state_->bloom_sampler ||
        !state_->tone_map_pipeline || !state_->tone_map_sampler || !state_->tone_map_constants) {
        set_diagnostics(diagnostics, "Runtime scene renderer is not initialized.");
        return false;
    }
    if (metrics.drawable_width == 0U || metrics.drawable_height == 0U) {
        state_->hiz_history.reset();
        if (diagnostics != nullptr) diagnostics->clear();
        return true;
    }
    if (!resize(metrics, diagnostics)) return false;
    if (!state_->scene_color || !state_->temporal_color_history[0] ||
        !state_->temporal_color_history[1] || !state_->depth_history[0] ||
        !state_->depth_history[1] || !state_->motion_vectors || !state_->bloom_extract ||
        !state_->bloom_blur || !state_->bloom_composite) {
        set_diagnostics(diagnostics, "VICE Runtime post-process frame targets are unavailable.");
        return false;
    }

    auto resident_scene_build = build_runtime_render_resident_scene(world);
    if (!resident_scene_build) {
        set_diagnostics(diagnostics, resident_scene_build.diagnostics);
        return false;
    }
    const auto& resident_scene = resident_scene_build.scene;
    auto resource_residency_build = build_runtime_render_resource_residency(
        resident_scene,
        state_->catalog,
        state_->materials.catalog(),
        state_->materials.texture_catalog(),
        state_->resource_budgets
    );
    if (!resource_residency_build) {
        set_diagnostics(diagnostics, resource_residency_build.diagnostics);
        return false;
    }
    const auto& resource_residency = resource_residency_build.residency;

    if (!state_->materials.set_texture_residency(
            resource_residency.textures.resources,
            diagnostics
        )) {
        return false;
    }
    const std::uint64_t texture_generation = state_->materials.texture_residency_generation();
    if (texture_generation != state_->gpu_material_texture_generation) {
        if (!state_->gpu_material_scene.refresh_resources(
                *state_->device,
                state_->materials,
                diagnostics
            )) {
            return false;
        }
        state_->gpu_material_texture_generation = texture_generation;
    }

    std::erase_if(state_->meshes, [&](const auto& entry) {
        return !resource_residency.geometry.resident(entry.first);
    });
    for (const world::ResourceId resource : resource_residency.geometry.resources) {
        if (state_->meshes.contains(resource)) continue;
        const auto* const record = state_->catalog.find(resource);
        if (record == nullptr) {
            set_diagnostics(
                diagnostics,
                "Runtime geometry residency references missing cooked mesh `" +
                    resource.value.to_string() + "`."
            );
            return false;
        }
        auto vertices = create_buffer_with_data(
            *state_->device,
            render::BufferUsage::vertex,
            std::as_bytes(std::span{record->asset.vertices}),
            "VICE Runtime resident mesh vertices"
        );
        auto indices = create_buffer_with_data(
            *state_->device,
            render::BufferUsage::index,
            std::as_bytes(std::span{record->asset.indices}),
            "VICE Runtime resident mesh indices"
        );
        if (!vertices || !indices) {
            set_diagnostics(
                diagnostics,
                "VICE Runtime could not upload resident cooked mesh `" +
                    record->path.generic_string() + "`."
            );
            return false;
        }
        state_->meshes.emplace(
            resource,
            State::MeshGpu{
                .vertices = std::move(vertices),
                .indices = std::move(indices),
                .index_count = static_cast<std::uint32_t>(record->asset.indices.size()),
                .bounds = runtime_mesh_bounding_sphere(record->asset.vertices),
            }
        );
    }

    const auto instances = filter_runtime_resident_geometry(
        resident_scene.meshes,
        resource_residency.geometry
    );
    for (const auto& instance : instances) {
        if (instance.material.has_value()) {
            const auto* const material_record = state_->materials.catalog().find(*instance.material);
            if (material_record == nullptr) {
                set_diagnostics(
                    diagnostics,
                    "Runtime World references missing cooked render material `" +
                        instance.material->value.to_string() + "`."
                );
                return false;
            }
            if (render::render_material_uses_gpu_scene_pbr(material_record->asset.version)) {
                if (state_->gpu_pbr_pipeline == nullptr || state_->gpu_pbr_frame == nullptr) {
                    set_diagnostics(
                        diagnostics,
                        "Runtime World references a V4 GPU PBR material but the shared GPU PBR pipeline is unavailable."
                    );
                    return false;
                }
            } else if (state_->materials.find(*instance.material).pipeline == nullptr) {
                set_diagnostics(
                    diagnostics,
                    "Runtime World references missing cooked render material `" +
                        instance.material->value.to_string() + "`."
                );
                return false;
            }
        }
    }

    const float aspect = static_cast<float>(metrics.drawable_width) /
        static_cast<float>(std::max(1U, metrics.drawable_height));
    const auto authored_camera = primary_camera_view(world, aspect);
    const RuntimeCameraView runtime_camera = authored_camera.has_value()
        ? *authored_camera
        : fallback_camera(instances, state_->catalog, aspect);
    const math::Mat4 unjittered_view_projection = runtime_camera.view_projection;
    const auto jitter = runtime_taa_jitter(
        state_->temporal_history.completed_frames(),
        metrics.drawable_width,
        metrics.drawable_height
    );
    const math::Mat4 view_projection = runtime_apply_projection_jitter(
        unjittered_view_projection,
        jitter
    );
    const math::Vec3 camera_position = runtime_camera.position;
    const auto temporal_frame = state_->temporal_history.begin_frame(
        runtime_camera.entity,
        unjittered_view_projection,
        view_projection,
        jitter,
        runtime_camera.position,
        runtime_camera.forward
    );
    auto* const scene_color = state_->scene_color.get();
    auto* const temporal_output = state_->temporal_color_history[state_->history_write_index].get();
    auto* const previous_scene_color = temporal_frame.history_valid
        ? state_->temporal_color_history[1U - state_->history_write_index].get()
        : scene_color;
    auto* const current_depth = state_->depth_history[state_->history_write_index].get();
    auto* const hiz_previous_depth =
        state_->depth_history[1U - state_->history_write_index].get();
    auto* const previous_depth = temporal_frame.history_valid
        ? state_->depth_history[1U - state_->history_write_index].get()
        : current_depth;

    const auto& authored_lights = resident_scene.lights;
    const auto shadow_view = directional_shadow_view(runtime_camera, authored_lights);
    const auto directional_shadow_path = resolve_runtime_directional_shadow_path(
        RuntimeDirectionalShadowMode::automatic,
        state_->device->capabilities().ray_tracing
    );
    const bool use_camera_depth_prepass = shadow_view.has_value() &&
        runtime_directional_shadow_requires_depth_prepass(directional_shadow_path);
    auto runtime_lights = authored_lights;
    if (runtime_lights.empty()) {
        runtime_lights.push_back(RuntimeLight{
            .light = world::Light{
                .type = world::LightType::directional,
                .color = {1.0F, 0.952381F, 0.880952F},
                .intensity = 4.2F,
                .range = 0.0F,
                .enabled = true,
            },
            .direction = {-0.431934F, -0.863868F, -0.259161F},
        });
    }
    const RuntimeAtmosphereFrame atmosphere = runtime_atmosphere_frame(runtime_lights);
    world::GlobalPosition camera_global_position{
        static_cast<double>(camera_position.x),
        static_cast<double>(camera_position.y),
        static_cast<double>(camera_position.z),
    };
    if (const auto camera_entity = world.primary_camera(); camera_entity.has_value()) {
        if (const auto spatial = world.spatial_state(*camera_entity); spatial.has_value()) {
            camera_global_position = spatial->global_position;
        }
    }
    const auto point_shadow_views = runtime_point_shadow_views(
        camera_global_position,
        runtime_lights
    );
    const auto global_environment = state_->environment.binding();
    const auto& reflection_probes = resident_scene.reflection_probes;
    for (const auto& probe : reflection_probes) {
        const auto binding = state_->environment.binding(probe.environment);
        if (!binding.available || binding.scope != render::EnvironmentScope::local_probe) {
            set_diagnostics(diagnostics,
                "Runtime reflection probe references a missing or non-local environment `" +
                probe.environment.value.to_string() + "`.");
            return false;
        }
    }

    auto commands = state_->device->create_command_list();
    if (!commands) {
        set_diagnostics(diagnostics, "VICE Runtime could not create scene commands.");
        return false;
    }

    const auto ray_tracing_plan = build_runtime_ray_tracing_scene_plan(
        instances,
        state_->catalog.records()
    );
    if (!ray_tracing_plan) {
        set_diagnostics(diagnostics, ray_tracing_plan.diagnostics);
        return false;
    }
    const auto ray_tracing_execution = state_->ray_tracing_scene.execute(
        *state_->device,
        *commands,
        ray_tracing_plan,
        [&](const RuntimeRayTracingBlasKey& key)
            -> std::optional<RuntimeRayTracingGeometryBinding> {
            const auto found = state_->meshes.find(key.mesh);
            if (found == state_->meshes.end() || !found->second.vertices || !found->second.indices) {
                return std::nullopt;
            }
            return RuntimeRayTracingGeometryBinding{
                .vertex_buffer = found->second.vertices.get(),
                .vertex_offset = 0U,
                .vertex_stride = static_cast<std::uint32_t>(sizeof(render::MeshVertex)),
                .vertex_format = render::VertexFormat::float32x3,
                .index_buffer = found->second.indices.get(),
                .index_offset = 0U,
                .index_format = render::IndexFormat::uint32,
                .opaque = true,
            };
        }
    );
    if (!ray_tracing_execution) {
        set_diagnostics(diagnostics, ray_tracing_execution.diagnostics);
        return false;
    }
    state_->ray_tracing_counters = ray_tracing_execution.counters;

    if (instances.size() > std::numeric_limits<std::uint32_t>::max()) {
        set_diagnostics(diagnostics, "Runtime scene instance count exceeds the GPU visibility contract.");
        return false;
    }
    const auto visibility_frustum = runtime_visibility_frustum(
        runtime_camera.forward,
        runtime_camera.up,
        runtime_camera.camera.vertical_fov_radians,
        runtime_camera.aspect_ratio,
        runtime_camera.camera.near_plane,
        runtime_camera.camera.far_plane
    );
    if (!valid(visibility_frustum)) {
        set_diagnostics(diagnostics, "VICE Runtime could not build the camera visibility frustum.");
        return false;
    }
    std::vector<RuntimeVisibilitySphere> visibility_spheres;
    visibility_spheres.reserve(instances.size());
    for (std::size_t instance_index = 0U; instance_index < instances.size(); ++instance_index) {
        const auto& instance = instances[instance_index];
        const auto& mesh = state_->meshes.at(instance.mesh);
        const auto sphere = runtime_transform_bounding_sphere(
            mesh.bounds,
            runtime_model_matrix_relative_to(instance, camera_global_position)
        );
        if (!valid(sphere)) {
            set_diagnostics(diagnostics, "VICE Runtime produced an invalid mesh visibility sphere.");
            return false;
        }
        visibility_spheres.push_back(RuntimeVisibilitySphere{
            .center = sphere.center,
            .radius = sphere.radius,
            .instance_index = static_cast<std::uint32_t>(instance_index),
        });
    }
    if (!state_->visibility.cull(
            *commands,
            visibility_spheres,
            visibility_frustum,
            diagnostics
        )) {
        return false;
    }

    RuntimeVisibilityGpuResult camera_visibility = state_->visibility.result();
    const bool use_hiz_occlusion = !instances.empty() && hiz_previous_depth != nullptr &&
        state_->hiz_history.usable(
            runtime_camera.entity,
            camera_global_position,
            runtime_camera.forward,
            runtime_camera.up,
            runtime_camera.camera,
            runtime_camera.aspect_ratio,
            instances
        );
    if (use_hiz_occlusion) {
        if (!state_->hiz_pyramid.build(*commands, *hiz_previous_depth, diagnostics)) {
            return false;
        }

        const auto& previous_camera = state_->hiz_history.camera();
        std::vector<RuntimeVisibilitySphere> previous_visibility_spheres;
        previous_visibility_spheres.reserve(instances.size());
        for (std::size_t instance_index = 0U; instance_index < instances.size(); ++instance_index) {
            const auto& instance = instances[instance_index];
            const auto& mesh = state_->meshes.at(instance.mesh);
            const auto sphere = runtime_transform_bounding_sphere(
                mesh.bounds,
                runtime_model_matrix_relative_to(instance, previous_camera.global_position)
            );
            if (!valid(sphere)) {
                set_diagnostics(
                    diagnostics,
                    "VICE Runtime produced an invalid previous-camera Hi-Z visibility sphere."
                );
                return false;
            }
            previous_visibility_spheres.push_back(RuntimeVisibilitySphere{
                .center = sphere.center,
                .radius = sphere.radius,
                .instance_index = static_cast<std::uint32_t>(instance_index),
            });
        }

        if (!state_->hiz_occlusion.cull(
                *commands,
                camera_visibility,
                previous_visibility_spheres,
                previous_camera.camera_relative_view_projection,
                previous_camera.near_plane,
                previous_camera.far_plane,
                state_->hiz_pyramid,
                0.001F,
                diagnostics
            )) {
            return false;
        }
        camera_visibility = state_->hiz_occlusion.result();
    }

    const auto gpu_cluster_candidate_build = build_runtime_gpu_cluster_candidate_plan(
        instances,
        state_->gpu_meshlet_plan
    );
    if (!gpu_cluster_candidate_build) {
        set_diagnostics(diagnostics, gpu_cluster_candidate_build.diagnostics);
        return false;
    }
    const auto& gpu_cluster_candidates = gpu_cluster_candidate_build.plan;
    std::vector<std::uint8_t> gpu_cluster_default_eligible(instances.size(), 0U);
    std::vector<std::uint8_t> gpu_cluster_pbr_eligible(instances.size(), 0U);
    for (std::size_t instance_index = 0U; instance_index < instances.size(); ++instance_index) {
        const auto& instance = instances[instance_index];
        const auto mesh_index = state_->gpu_meshlet_plan.mesh_index(instance.mesh);
        if (!mesh_index.has_value() || *mesh_index >= state_->gpu_meshlet_plan.meshes.size()) {
            set_diagnostics(diagnostics, "Runtime cluster submission could not resolve a mesh table index.");
            return false;
        }
        if (state_->gpu_meshlet_plan.meshes[*mesh_index].meshlet_count == 0U) continue;
        if (!instance.material.has_value()) {
            gpu_cluster_default_eligible[instance_index] = 1U;
            continue;
        }
        const auto* const material = state_->materials.catalog().find(*instance.material);
        if (material != nullptr && render::render_material_uses_gpu_scene_pbr(material->asset.version)) {
            gpu_cluster_pbr_eligible[instance_index] = 1U;
        }
    }
    const auto gpu_cluster_default_build = build_runtime_gpu_cluster_batch_plan(
        gpu_cluster_candidates,
        state_->gpu_meshlet_plan,
        gpu_cluster_default_eligible
    );
    const auto gpu_cluster_pbr_build = build_runtime_gpu_cluster_batch_plan(
        gpu_cluster_candidates,
        state_->gpu_meshlet_plan,
        gpu_cluster_pbr_eligible
    );
    if (!gpu_cluster_default_build || !gpu_cluster_pbr_build) {
        set_diagnostics(
            diagnostics,
            !gpu_cluster_default_build
                ? gpu_cluster_default_build.diagnostics
                : gpu_cluster_pbr_build.diagnostics
        );
        return false;
    }
    const auto& gpu_cluster_default_plan = gpu_cluster_default_build.plan;
    const auto& gpu_cluster_pbr_plan = gpu_cluster_pbr_build.plan;

    std::vector<math::Mat4> gpu_instance_models;
    gpu_instance_models.reserve(instances.size());
    std::vector<math::Mat4> gpu_cluster_models;
    gpu_cluster_models.reserve(instances.size());
    std::vector<std::uint32_t> gpu_batch_assignments(
        instances.size(), runtime_gpu_draw_batch_unassigned
    );
    std::vector<RuntimeGpuDrawBatch> gpu_default_batches;
    std::vector<world::ResourceId> gpu_default_batch_meshes;
    std::unordered_map<world::ResourceId, std::uint32_t, world::ResourceIdHash> gpu_default_batch_lookup;
    for (std::size_t instance_index = 0U; instance_index < instances.size(); ++instance_index) {
        const auto& instance = instances[instance_index];
        gpu_instance_models.push_back(runtime_model_matrix(instance));
        gpu_cluster_models.push_back(runtime_model_matrix_relative_to(instance, camera_global_position));
        if (instance.material.has_value() || gpu_cluster_default_eligible[instance_index] != 0U) continue;

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

    const auto gpu_pbr_build = detail::build_runtime_gpu_pbr_batch_plan(
        instances,
        state_->catalog.records(),
        state_->materials.catalog().records()
    );
    if (!gpu_pbr_build) {
        set_diagnostics(diagnostics, gpu_pbr_build.diagnostics);
        return false;
    }
    auto gpu_pbr_plan = std::move(gpu_pbr_build.plan);
    for (std::size_t instance_index = 0U; instance_index < instances.size(); ++instance_index) {
        if (gpu_cluster_pbr_eligible[instance_index] != 0U) {
            gpu_pbr_plan.instance_batches[instance_index] = runtime_gpu_draw_batch_unassigned;
        }
    }
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
    RuntimeGpuDrawBatchResult gpu_cluster_default_batch_result{};
    RuntimeGpuDrawBatchResult gpu_cluster_pbr_batch_result{};
    RuntimeGpuDrawBatchResult gpu_shadow_batch_result{};
    RuntimeGpuDrawBatchResult gpu_motion_batch_result{};
    RuntimeGpuClusterVisibilityResult gpu_cluster_visibility_result{};
    if (!instances.empty()) {
        if (!state_->gpu_instances.upload(*commands, gpu_instance_models, diagnostics) ||
            !state_->gpu_cluster_instances.upload(*commands, gpu_cluster_models, diagnostics)) {
            return false;
        }
    }
    if (!gpu_cluster_candidates.candidates.empty() &&
        (!gpu_cluster_default_plan.batches.empty() || !gpu_cluster_pbr_plan.batches.empty())) {
        if (!state_->gpu_cluster_visibility.cull(
                *commands,
                gpu_cluster_candidates.candidates,
                camera_visibility,
                state_->gpu_meshlet_table.result(),
                state_->gpu_cluster_instances.result(),
                visibility_frustum,
                diagnostics
            )) {
            return false;
        }
        gpu_cluster_visibility_result = state_->gpu_cluster_visibility.result();
    }
    if (!gpu_shadow_plan.batches.empty()) {
        const auto gpu_previous_build = detail::build_runtime_gpu_previous_models(
            instances,
            gpu_instance_models,
            state_->previous_models,
            temporal_frame.history_valid
        );
        if (!gpu_previous_build) {
            set_diagnostics(diagnostics, gpu_previous_build.diagnostics);
            return false;
        }
        if (!state_->gpu_previous_instances.upload(
                *commands, gpu_previous_build.models, diagnostics
            ) || !state_->gpu_motion_draw_batches.build(
                *commands,
                camera_visibility,
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
    if (!gpu_default_batches.empty()) {
        if (!state_->gpu_draw_batches.build(
                *commands,
                camera_visibility,
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
            set_diagnostics(diagnostics, "VICE Runtime could not update GPU default frame constants.");
            return false;
        }
        gpu_default_batch_result = state_->gpu_draw_batches.result();
    }
    if (!gpu_cluster_default_plan.batches.empty()) {
        if (!state_->gpu_cluster_default_draw_batches.build(
                *commands,
                gpu_cluster_visibility_result.visibility,
                gpu_cluster_default_plan.candidate_batches,
                gpu_cluster_default_plan.batches,
                diagnostics
            ) || !state_->gpu_cluster_default_instances.remap(
                *commands,
                state_->gpu_cluster_default_draw_batches.result(),
                gpu_cluster_visibility_result,
                diagnostics
            )) {
            return false;
        }
        const GpuDefaultFrame gpu_frame{.view_projection = view_projection};
        if (!state_->gpu_default_frame->write(
                0U, std::as_bytes(std::span{&gpu_frame, 1})
            )) {
            set_diagnostics(diagnostics, "VICE Runtime could not update GPU default cluster frame constants.");
            return false;
        }
        gpu_cluster_default_batch_result = state_->gpu_cluster_default_draw_batches.result();
    }
    if (!gpu_pbr_plan.batches.empty()) {
        if (!state_->gpu_pbr_draw_batches.build(
                *commands,
                camera_visibility,
                gpu_pbr_plan.instance_batches,
                gpu_pbr_plan.batches,
                diagnostics
            ) || !state_->gpu_material_scene.upload(*commands, instances, diagnostics)) {
            return false;
        }
        const GpuPbrSceneConstants gpu_pbr_constants = make_gpu_pbr_scene_constants(
            view_projection,
            runtime_camera,
            runtime_lights,
            shadow_view,
            point_shadow_views,
            atmosphere,
            global_environment
        );
        if (!state_->gpu_pbr_frame->write(
                0U, std::as_bytes(std::span{&gpu_pbr_constants, 1})
            )) {
            set_diagnostics(diagnostics, "VICE Runtime could not update GPU PBR scene constants.");
            return false;
        }
        gpu_pbr_batch_result = state_->gpu_pbr_draw_batches.result();
    }
    if (!gpu_cluster_pbr_plan.batches.empty()) {
        if (!state_->gpu_cluster_pbr_draw_batches.build(
                *commands,
                gpu_cluster_visibility_result.visibility,
                gpu_cluster_pbr_plan.candidate_batches,
                gpu_cluster_pbr_plan.batches,
                diagnostics
            ) || !state_->gpu_cluster_pbr_instances.remap(
                *commands,
                state_->gpu_cluster_pbr_draw_batches.result(),
                gpu_cluster_visibility_result,
                diagnostics
            )) {
            return false;
        }
        gpu_cluster_pbr_batch_result = state_->gpu_cluster_pbr_draw_batches.result();
    }
    const bool has_point_shadows = std::any_of(
        point_shadow_views.begin(),
        point_shadow_views.end(),
        [](const auto& view) { return view.has_value(); }
    );
    if ((shadow_view.has_value() || has_point_shadows) && !gpu_shadow_plan.batches.empty()) {
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
        gpu_shadow_batch_result = state_->gpu_shadow_draw_batches.result();
    }
    if (shadow_view.has_value() && !gpu_shadow_plan.batches.empty()) {
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
    }
    if (has_point_shadows && !gpu_shadow_plan.batches.empty()) {
        for (std::size_t light_slot = 0U; light_slot < point_shadow_views.size(); ++light_slot) {
            if (!point_shadow_views[light_slot].has_value()) continue;
            const auto& point_view = *point_shadow_views[light_slot];
            std::vector<math::Mat4> point_models;
            point_models.reserve(instances.size());
            for (const auto& instance : instances) {
                point_models.push_back(
                    runtime_model_matrix_relative_to(instance, point_view.global_position)
                );
            }
            if (!state_->gpu_point_shadow_instances[light_slot].upload(
                    *commands, point_models, diagnostics
                )) {
                return false;
            }
            for (std::size_t face_index = 0U; face_index < runtime_point_shadow_face_count; ++face_index) {
                const GpuDefaultFrame point_frame{
                    .view_projection = point_view.face_view_projections[face_index],
                };
                if (!state_->gpu_point_shadow_frames[light_slot][face_index]->write(
                        0U, std::as_bytes(std::span{&point_frame, 1})
                    )) {
                    set_diagnostics(diagnostics, "VICE Runtime could not update GPU point-shadow frame constants.");
                    return false;
                }
            }
        }
    }

    if (shadow_view.has_value()) {
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

    for (std::size_t light_slot = 0U; light_slot < point_shadow_views.size(); ++light_slot) {
        if (!point_shadow_views[light_slot].has_value()) continue;
        const auto point_instance_table = state_->gpu_point_shadow_instances[light_slot].result();
        if (!gpu_shadow_plan.batches.empty() &&
            (point_instance_table.models == nullptr ||
             gpu_shadow_batch_result.compacted_instance_indices == nullptr ||
             gpu_shadow_batch_result.indirect_arguments == nullptr)) {
            set_diagnostics(diagnostics, "VICE Runtime GPU point-shadow batch resources are unavailable.");
            return false;
        }
        const render::BufferViewDesc point_model_view{
            .offset = 0U,
            .size = static_cast<std::uint64_t>(instances.size()) * sizeof(math::Mat4),
            .structure_stride = sizeof(math::Mat4),
        };
        const render::BufferViewDesc point_compacted_view{
            .offset = 0U,
            .size = static_cast<std::uint64_t>(instances.size()) * sizeof(std::uint32_t),
            .structure_stride = sizeof(std::uint32_t),
        };
        for (std::size_t face_index = 0U; face_index < runtime_point_shadow_face_count; ++face_index) {
            if (!commands->begin_depth_render_pass(
                    *state_->point_shadow_maps[light_slot][face_index], 1.0F, true
                ) || !commands->set_viewport(render::Viewport{
                    .width = static_cast<float>(runtime_point_shadow_map_size),
                    .height = static_cast<float>(runtime_point_shadow_map_size),
                }) || !commands->set_scissor_rect(render::ScissorRect{
                    .width = runtime_point_shadow_map_size,
                    .height = runtime_point_shadow_map_size,
                })) {
                set_diagnostics(diagnostics, "VICE Runtime could not begin a point-light shadow face.");
                return false;
            }
            if (!gpu_shadow_plan.batches.empty()) {
                if (!commands->set_graphics_pipeline(*state_->gpu_shadow_pipeline) ||
                    !commands->set_graphics_constant_buffer(
                        frame_binding,
                        *state_->gpu_point_shadow_frames[light_slot][face_index],
                        0U,
                        sizeof(GpuDefaultFrame)
                    ) || !commands->set_graphics_buffer(
                        gpu_model_table_binding,
                        *point_instance_table.models,
                        point_model_view
                    ) || !commands->set_graphics_buffer(
                        gpu_compacted_instance_binding,
                        *gpu_shadow_batch_result.compacted_instance_indices,
                        point_compacted_view
                    )) {
                    set_diagnostics(diagnostics, "VICE Runtime could not bind GPU point-shadow resources.");
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
                        set_diagnostics(diagnostics, "VICE Runtime could not encode a GPU-driven point-shadow batch.");
                        return false;
                    }
                }
            }
            if (!commands->end_render_pass()) {
                set_diagnostics(diagnostics, "VICE Runtime could not finish a point-light shadow face.");
                return false;
            }
        }
    }

    bool motion_vectors_encoded = false;
    const auto encode_motion_vectors = [&]() -> bool {
        render::RenderPassDesc motion_pass;
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

        motion_vectors_encoded = true;
        return true;
    };

    if (use_camera_depth_prepass) {
        const GpuDefaultFrame depth_frame{.view_projection = view_projection};
        if (!state_->gpu_default_frame->write(
                0U, std::as_bytes(std::span{&depth_frame, 1})
            ) || !commands->begin_depth_render_pass(*current_depth, 1.0F, true) ||
            !commands->set_viewport(render::Viewport{
                .width = static_cast<float>(metrics.drawable_width),
                .height = static_cast<float>(metrics.drawable_height),
            }) || !commands->set_scissor_rect(render::ScissorRect{
                .width = metrics.drawable_width,
                .height = metrics.drawable_height,
            })) {
            set_diagnostics(diagnostics, "VICE Runtime could not begin the camera depth prepass.");
            return false;
        }

        if (!gpu_shadow_plan.batches.empty()) {
            const auto instance_table = state_->gpu_instances.result();
            if (state_->gpu_shadow_pipeline == nullptr || instance_table.models == nullptr ||
                gpu_motion_batch_result.compacted_instance_indices == nullptr ||
                gpu_motion_batch_result.indirect_arguments == nullptr) {
                set_diagnostics(diagnostics, "VICE Runtime camera depth-prepass resources are unavailable.");
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
            if (!commands->set_graphics_pipeline(*state_->gpu_shadow_pipeline) ||
                !commands->set_graphics_constant_buffer(
                    frame_binding, *state_->gpu_default_frame, 0U, sizeof(GpuDefaultFrame)
                ) || !commands->set_graphics_buffer(
                    gpu_model_table_binding, *instance_table.models, model_view
                ) || !commands->set_graphics_buffer(
                    gpu_compacted_instance_binding,
                    *gpu_motion_batch_result.compacted_instance_indices,
                    compacted_view
                )) {
                set_diagnostics(diagnostics, "VICE Runtime could not bind camera depth-prepass resources.");
                return false;
            }
            for (std::size_t batch_index = 0U; batch_index < gpu_shadow_plan.batches.size(); ++batch_index) {
                auto& mesh = state_->meshes.at(gpu_shadow_plan.batch_meshes[batch_index]);
                const std::uint64_t argument_offset = static_cast<std::uint64_t>(batch_index) *
                    sizeof(render::IndexedIndirectDrawArguments);
                if (!commands->set_vertex_buffer(0U, *mesh.vertices) ||
                    !commands->set_index_buffer(*mesh.indices, render::IndexFormat::uint32) ||
                    !commands->draw_indexed_indirect(
                        *gpu_motion_batch_result.indirect_arguments, argument_offset
                    )) {
                    set_diagnostics(diagnostics, "VICE Runtime could not encode a camera depth-prepass batch.");
                    return false;
                }
            }
        }
        if (!commands->end_render_pass()) {
            set_diagnostics(diagnostics, "VICE Runtime could not finish the camera depth prepass.");
            return false;
        }
        // Current depth is authoritative here, so Stage E temporal consumers can use the
        // same motion texture before forward PBR without a second motion representation.
        if (!encode_motion_vectors()) return false;
    }

    const math::Vec3 camera_right = math::normalize(math::cross(runtime_camera.forward, runtime_camera.up));
    detail::SkyConstants sky_constants{};
    sky_constants.camera_forward_tan_half_fov[0] = runtime_camera.forward.x;
    sky_constants.camera_forward_tan_half_fov[1] = runtime_camera.forward.y;
    sky_constants.camera_forward_tan_half_fov[2] = runtime_camera.forward.z;
    sky_constants.camera_forward_tan_half_fov[3] = std::tan(runtime_camera.camera.vertical_fov_radians * 0.5F);
    sky_constants.camera_right_aspect[0] = camera_right.x;
    sky_constants.camera_right_aspect[1] = camera_right.y;
    sky_constants.camera_right_aspect[2] = camera_right.z;
    sky_constants.camera_right_aspect[3] = runtime_camera.aspect_ratio;
    sky_constants.camera_up_rayleigh[0] = runtime_camera.up.x;
    sky_constants.camera_up_rayleigh[1] = runtime_camera.up.y;
    sky_constants.camera_up_rayleigh[2] = runtime_camera.up.z;
    sky_constants.camera_up_rayleigh[3] = atmosphere.settings.rayleigh_strength;
    sky_constants.sun_direction_mie[0] = atmosphere.sun_direction.x;
    sky_constants.sun_direction_mie[1] = atmosphere.sun_direction.y;
    sky_constants.sun_direction_mie[2] = atmosphere.sun_direction.z;
    sky_constants.sun_direction_mie[3] = atmosphere.settings.mie_strength;
    sky_constants.sun_color_intensity[0] = atmosphere.sun_color.x;
    sky_constants.sun_color_intensity[1] = atmosphere.sun_color.y;
    sky_constants.sun_color_intensity[2] = atmosphere.sun_color.z;
    sky_constants.sun_color_intensity[3] = atmosphere.sun_intensity;
    sky_constants.atmosphere_parameters[0] = atmosphere.settings.mie_anisotropy;
    sky_constants.atmosphere_parameters[1] = atmosphere.settings.sun_angular_radius;
    sky_constants.atmosphere_parameters[2] = atmosphere.settings.aerial_density;
    sky_constants.atmosphere_parameters[3] = atmosphere.settings.horizon_falloff;
    sky_constants.ground_albedo[0] = atmosphere.settings.ground_albedo.x;
    sky_constants.ground_albedo[1] = atmosphere.settings.ground_albedo.y;
    sky_constants.ground_albedo[2] = atmosphere.settings.ground_albedo.z;
    sky_constants.ground_albedo[3] = 1.0F;
    if (!state_->sky_constants->write(0U, std::as_bytes(std::span{&sky_constants, 1}))) {
        set_diagnostics(diagnostics, "VICE Runtime could not update sky/atmosphere constants.");
        return false;
    }

    render::RenderPassDesc sky_pass;
    sky_pass.clear_color = clear_color;
    if (!commands->begin_render_pass(*scene_color, sky_pass) ||
        !commands->set_viewport(render::Viewport{
            .width = static_cast<float>(metrics.drawable_width),
            .height = static_cast<float>(metrics.drawable_height),
        }) ||
        !commands->set_scissor_rect(render::ScissorRect{
            .width = metrics.drawable_width,
            .height = metrics.drawable_height,
        }) ||
        !commands->set_graphics_pipeline(*state_->sky_pipeline) ||
        !commands->set_graphics_constant_buffer(
            detail::sky_constants_binding,
            *state_->sky_constants,
            0U,
            sizeof(detail::SkyConstants)
        ) ||
        !commands->draw(3U) || !commands->end_render_pass()) {
        set_diagnostics(diagnostics, "VICE Runtime could not execute the HDR sky/atmosphere pass.");
        return false;
    }

    render::RenderPassDesc pass;
    pass.clear_color = clear_color;
    pass.depth_attachment = current_depth;
    pass.clear_depth = 1.0F;
    pass.clear_color_attachment = false;
    pass.clear_depth_attachment = !use_camera_depth_prepass;
    if (!commands->begin_render_pass(*scene_color, pass) ||
        !commands->set_viewport(render::Viewport{
            .width = static_cast<float>(metrics.drawable_width),
            .height = static_cast<float>(metrics.drawable_height),
        }) ||
        !commands->set_scissor_rect(render::ScissorRect{
            .width = metrics.drawable_width,
            .height = metrics.drawable_height,
        })) {
        set_diagnostics(diagnostics, "VICE Runtime could not begin the HDR scene render pass.");
        return false;
    }

    if (!gpu_default_batches.empty()) {
        const auto instance_table = state_->gpu_instances.result();
        if (instance_table.models == nullptr || gpu_default_batch_result.compacted_instance_indices == nullptr ||
            gpu_default_batch_result.indirect_arguments == nullptr) {
            set_diagnostics(diagnostics, "VICE Runtime GPU default batch resources are unavailable.");
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
            set_diagnostics(diagnostics, "VICE Runtime could not bind GPU default scene resources.");
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
                    "VICE Runtime could not encode a GPU-driven default mesh batch."
                );
                return false;
            }
        }
    }

    if (!gpu_cluster_default_plan.batches.empty()) {
        const auto instance_table = state_->gpu_instances.result();
        const auto remapped_instances = state_->gpu_cluster_default_instances.result();
        if (instance_table.models == nullptr || remapped_instances.scene_instance_indices == nullptr ||
            gpu_cluster_default_batch_result.indirect_arguments == nullptr) {
            set_diagnostics(diagnostics, "VICE Runtime GPU default cluster resources are unavailable.");
            return false;
        }
        const render::BufferViewDesc model_view{
            .offset = 0U,
            .size = static_cast<std::uint64_t>(instances.size()) * sizeof(math::Mat4),
            .structure_stride = sizeof(math::Mat4),
        };
        const render::BufferViewDesc compacted_view{
            .offset = 0U,
            .size = static_cast<std::uint64_t>(gpu_cluster_candidates.candidates.size()) * sizeof(std::uint32_t),
            .structure_stride = sizeof(std::uint32_t),
        };
        if (!commands->set_graphics_pipeline(*state_->pipeline) ||
            !commands->set_graphics_constant_buffer(
                frame_binding, *state_->gpu_default_frame, 0U, sizeof(GpuDefaultFrame)
            ) || !commands->set_graphics_buffer(
                gpu_model_table_binding, *instance_table.models, model_view
            ) || !commands->set_graphics_buffer(
                gpu_compacted_instance_binding,
                *remapped_instances.scene_instance_indices,
                compacted_view
            )) {
            set_diagnostics(diagnostics, "VICE Runtime could not bind GPU default cluster resources.");
            return false;
        }
        for (std::size_t batch_index = 0U; batch_index < gpu_cluster_default_plan.batches.size(); ++batch_index) {
            auto& mesh = state_->meshes.at(gpu_cluster_default_plan.batch_meshes[batch_index]);
            const std::uint64_t argument_offset = static_cast<std::uint64_t>(batch_index) *
                sizeof(render::IndexedIndirectDrawArguments);
            if (!commands->set_vertex_buffer(0U, *mesh.vertices) ||
                !commands->set_index_buffer(*mesh.indices, render::IndexFormat::uint32) ||
                !commands->draw_indexed_indirect(
                    *gpu_cluster_default_batch_result.indirect_arguments,
                    argument_offset
                )) {
                set_diagnostics(diagnostics, "VICE Runtime could not encode a GPU-driven default cluster batch.");
                return false;
            }
        }
    }

    if (!gpu_pbr_plan.batches.empty()) {
        const auto instance_table = state_->gpu_instances.result();
        const auto material_scene = state_->gpu_material_scene.view();
        if (state_->gpu_pbr_pipeline == nullptr || state_->gpu_pbr_frame == nullptr ||
            instance_table.models == nullptr ||
            gpu_pbr_batch_result.compacted_instance_indices == nullptr ||
            gpu_pbr_batch_result.indirect_arguments == nullptr ||
            material_scene.materials.records == nullptr ||
            material_scene.instance_materials.records == nullptr ||
            material_scene.resources.textures.empty() ||
            material_scene.resources.samplers.empty()) {
            set_diagnostics(diagnostics, "VICE Runtime GPU PBR resources are unavailable.");
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
        const render::BufferViewDesc instance_material_view{
            .offset = 0U,
            .size = static_cast<std::uint64_t>(instances.size()) *
                sizeof(RuntimeGpuInstanceMaterialRecord),
            .structure_stride = sizeof(RuntimeGpuInstanceMaterialRecord),
        };
        const render::BufferViewDesc material_view{
            .offset = 0U,
            .size = static_cast<std::uint64_t>(material_scene.materials.material_count) *
                sizeof(RuntimeGpuMaterialRecord),
            .structure_stride = sizeof(RuntimeGpuMaterialRecord),
        };

        bool gpu_pbr_bound =
            commands->set_graphics_pipeline(*state_->gpu_pbr_pipeline) &&
            commands->set_graphics_constant_buffer(
                frame_binding, *state_->gpu_pbr_frame, 0U, sizeof(GpuPbrSceneConstants)
            ) &&
            commands->set_graphics_buffer(
                gpu_pbr_model_table_binding, *instance_table.models, model_view
            ) &&
            commands->set_graphics_buffer(
                gpu_pbr_compacted_instance_binding,
                *gpu_pbr_batch_result.compacted_instance_indices,
                compacted_view
            ) &&
            commands->set_graphics_buffer(
                gpu_pbr_instance_material_binding,
                *material_scene.instance_materials.records,
                instance_material_view
            ) &&
            commands->set_graphics_buffer(
                gpu_pbr_material_record_binding,
                *material_scene.materials.records,
                material_view
            );

        for (std::size_t texture_index = 0U;
            texture_index < material_scene.resources.textures.size() && gpu_pbr_bound;
            ++texture_index) {
            gpu_pbr_bound = material_scene.resources.textures[texture_index] != nullptr &&
                commands->set_graphics_texture(
                    gpu_pbr_material_texture_binding(static_cast<std::uint32_t>(texture_index)),
                    *material_scene.resources.textures[texture_index]
                );
        }
        for (std::size_t sampler_index = 0U;
            sampler_index < material_scene.resources.samplers.size() && gpu_pbr_bound;
            ++sampler_index) {
            gpu_pbr_bound = material_scene.resources.samplers[sampler_index] != nullptr &&
                commands->set_graphics_sampler(
                    gpu_pbr_material_sampler_binding(static_cast<std::uint32_t>(sampler_index)),
                    *material_scene.resources.samplers[sampler_index]
                );
        }

        for (std::size_t cascade_index = 0U;
            cascade_index < runtime_shadow_cascade_count && gpu_pbr_bound;
            ++cascade_index) {
            gpu_pbr_bound = state_->shadow_maps[cascade_index] != nullptr &&
                commands->set_graphics_texture(
                    gpu_pbr_lighting_texture_binding(static_cast<std::uint32_t>(cascade_index)),
                    *state_->shadow_maps[cascade_index]
                );
        }
        for (std::size_t light_slot = 0U;
            light_slot < runtime_point_shadow_light_count && gpu_pbr_bound;
            ++light_slot) {
            for (std::size_t face_index = 0U;
                face_index < runtime_point_shadow_face_count && gpu_pbr_bound;
                ++face_index) {
                const std::uint32_t binding = 4U + static_cast<std::uint32_t>(
                    light_slot * runtime_point_shadow_face_count + face_index
                );
                gpu_pbr_bound = state_->point_shadow_maps[light_slot][face_index] != nullptr &&
                    commands->set_graphics_texture(
                        gpu_pbr_lighting_texture_binding(binding),
                        *state_->point_shadow_maps[light_slot][face_index]
                    );
            }
        }
        gpu_pbr_bound = gpu_pbr_bound && state_->shadow_sampler != nullptr &&
            commands->set_graphics_sampler(
                gpu_pbr_lighting_sampler_binding(0U), *state_->shadow_sampler
            );

        render::Texture* environment_irradiance = material_scene.resources.textures.front();
        render::Texture* environment_specular = environment_irradiance;
        render::Texture* environment_brdf = environment_irradiance;
        render::Sampler* environment_sampler = material_scene.resources.samplers.front();
        if (global_environment.available) {
            environment_irradiance = global_environment.irradiance;
            environment_specular = global_environment.specular;
            environment_brdf = global_environment.brdf_lut;
            environment_sampler = global_environment.sampler;
        }
        gpu_pbr_bound = gpu_pbr_bound &&
            environment_irradiance != nullptr &&
            environment_specular != nullptr &&
            environment_brdf != nullptr &&
            environment_sampler != nullptr;
        for (std::size_t source_index = 0U; source_index < 3U && gpu_pbr_bound; ++source_index) {
            const std::uint32_t base = 16U + static_cast<std::uint32_t>(source_index) * 3U;
            gpu_pbr_bound =
                commands->set_graphics_texture(
                    gpu_pbr_lighting_texture_binding(base), *environment_irradiance
                ) &&
                commands->set_graphics_texture(
                    gpu_pbr_lighting_texture_binding(base + 1U), *environment_specular
                ) &&
                commands->set_graphics_texture(
                    gpu_pbr_lighting_texture_binding(base + 2U), *environment_brdf
                );
        }
        gpu_pbr_bound = gpu_pbr_bound && commands->set_graphics_sampler(
            gpu_pbr_lighting_sampler_binding(1U), *environment_sampler
        );
        if (!gpu_pbr_bound) {
            set_diagnostics(diagnostics, "VICE Runtime could not bind indexed GPU PBR resources.");
            return false;
        }

        for (std::size_t batch_index = 0U; batch_index < gpu_pbr_plan.batches.size(); ++batch_index) {
            auto& mesh = state_->meshes.at(gpu_pbr_plan.batch_meshes[batch_index]);
            const std::uint64_t argument_offset = static_cast<std::uint64_t>(batch_index) *
                sizeof(render::IndexedIndirectDrawArguments);
            if (!commands->set_vertex_buffer(0U, *mesh.vertices) ||
                !commands->set_index_buffer(*mesh.indices, render::IndexFormat::uint32) ||
                !commands->draw_indexed_indirect(
                    *gpu_pbr_batch_result.indirect_arguments,
                    argument_offset
                )) {
                set_diagnostics(diagnostics, "VICE Runtime could not encode an indexed GPU PBR batch.");
                return false;
            }
        }

        if (!gpu_cluster_pbr_plan.batches.empty()) {
            const auto remapped_instances = state_->gpu_cluster_pbr_instances.result();
            if (remapped_instances.scene_instance_indices == nullptr ||
                gpu_cluster_pbr_batch_result.indirect_arguments == nullptr) {
                set_diagnostics(diagnostics, "VICE Runtime GPU PBR cluster resources are unavailable.");
                return false;
            }
            const render::BufferViewDesc cluster_compacted_view{
                .offset = 0U,
                .size = static_cast<std::uint64_t>(gpu_cluster_candidates.candidates.size()) *
                    sizeof(std::uint32_t),
                .structure_stride = sizeof(std::uint32_t),
            };
            if (!commands->set_graphics_buffer(
                    gpu_pbr_compacted_instance_binding,
                    *remapped_instances.scene_instance_indices,
                    cluster_compacted_view
                )) {
                set_diagnostics(diagnostics, "VICE Runtime could not bind GPU PBR cluster instance indices.");
                return false;
            }
            for (std::size_t batch_index = 0U;
                 batch_index < gpu_cluster_pbr_plan.batches.size();
                 ++batch_index) {
                auto& mesh = state_->meshes.at(gpu_cluster_pbr_plan.batch_meshes[batch_index]);
                const std::uint64_t argument_offset = static_cast<std::uint64_t>(batch_index) *
                    sizeof(render::IndexedIndirectDrawArguments);
                if (!commands->set_vertex_buffer(0U, *mesh.vertices) ||
                    !commands->set_index_buffer(*mesh.indices, render::IndexFormat::uint32) ||
                    !commands->draw_indexed_indirect(
                        *gpu_cluster_pbr_batch_result.indirect_arguments,
                        argument_offset
                    )) {
                    set_diagnostics(diagnostics, "VICE Runtime could not encode an indexed GPU PBR cluster batch.");
                    return false;
                }
            }
        }
    }

    for (const auto& instance : instances) {
        if (!instance.material.has_value()) continue;
        const auto* const direct_material = state_->materials.catalog().find(*instance.material);
        if (direct_material != nullptr &&
            render::render_material_uses_gpu_scene_pbr(direct_material->asset.version)) {
            continue;
        }
        auto constants_iterator = state_->constants.find(instance.entity);
        if (constants_iterator == state_->constants.end()) {
            auto constants = create_constant_buffer(*state_->device);
            if (!constants) {
                set_diagnostics(diagnostics, "VICE Runtime could not create scene constants.");
                return false;
            }
            constants_iterator = state_->constants.emplace(instance.entity, std::move(constants)).first;
        }

        render::GraphicsPipeline* pipeline = state_->pipeline.get();
        detail::RuntimeMaterialBinding material_state{};
        if (instance.material.has_value()) {
            material_state = state_->materials.find(*instance.material);
            pipeline = material_state.pipeline;
        }

        std::array<detail::RuntimeEnvironmentBinding, 3U> environment_sources{};
        std::array<std::optional<std::size_t>, 3U> environment_probe_indices{};
        std::array<float, 3U> environment_weights{};
        if (material_state.environment_source_count > 0U) {
            struct WeightedEnvironment {
                detail::RuntimeEnvironmentBinding binding{};
                std::optional<std::size_t> probe_index;
                float weight = 0.0F;
            };
            std::vector<WeightedEnvironment> weighted;
            const auto blend = select_runtime_reflection_probes(
                instance.spatial.global_position,
                reflection_probes
            );
            for (std::size_t probe_index = 0U; probe_index < blend.local_environments.size(); ++probe_index) {
                if (!blend.local_environments[probe_index].has_value() || blend.local_weights[probe_index] <= 0.0F) continue;
                const auto local = state_->environment.binding(*blend.local_environments[probe_index]);
                const auto selected_probe = blend.local_probe_indices[probe_index];
                if (local.available && selected_probe.has_value() && *selected_probe < reflection_probes.size()) {
                    weighted.push_back({local, selected_probe, blend.local_weights[probe_index]});
                }
            }
            if (global_environment.available && blend.global_weight > 0.0F) {
                weighted.push_back({global_environment, std::nullopt, blend.global_weight});
            }
            if (weighted.empty()) {
                set_diagnostics(diagnostics,
                    "Environment-aware material has no global or local environment for mesh `" +
                    instance.mesh.value.to_string() + "`.");
                return false;
            }
            float total_weight = 0.0F;
            for (const auto& source : weighted) total_weight += source.weight;
            if (total_weight <= 0.0F) total_weight = 1.0F;
            for (auto& source : weighted) source.weight /= total_weight;
            std::sort(weighted.begin(), weighted.end(), [](const auto& left, const auto& right) {
                return left.weight > right.weight;
            });
            if (material_state.environment_source_count == 1U) {
                environment_sources[0] = weighted.front().binding;
                environment_probe_indices[0] = weighted.front().probe_index;
                environment_weights[0] = 1.0F;
            } else {
                const std::size_t count = std::min<std::size_t>(3U, weighted.size());
                for (std::size_t source_index = 0U; source_index < count; ++source_index) {
                    environment_sources[source_index] = weighted[source_index].binding;
                    environment_probe_indices[source_index] = weighted[source_index].probe_index;
                    environment_weights[source_index] = weighted[source_index].weight;
                }
                for (std::size_t source_index = count; source_index < 3U; ++source_index) {
                    environment_sources[source_index] = environment_sources[0];
                    environment_probe_indices[source_index].reset();
                    environment_weights[source_index] = 0.0F;
                }
            }
        }

        SceneConstants constants{
            .view_projection = view_projection,
            .model = runtime_model_matrix(instance),
            .camera_position = {camera_position.x, camera_position.y, camera_position.z, 1.0F},
        };
        constants.light_metadata[0] = static_cast<float>(runtime_lights.size());
        for (std::size_t light_index = 0; light_index < runtime_lights.size(); ++light_index) {
            const auto& source = runtime_lights[light_index];
            auto& destination = constants.lights[light_index];
            if (source.light.type == world::LightType::directional) {
                destination.position_or_direction[0] = source.direction.x;
                destination.position_or_direction[1] = source.direction.y;
                destination.position_or_direction[2] = source.direction.z;
                destination.position_or_direction[3] = 0.0F;
            } else {
                destination.position_or_direction[0] = source.position.x;
                destination.position_or_direction[1] = source.position.y;
                destination.position_or_direction[2] = source.position.z;
                destination.position_or_direction[3] = 1.0F;
            }
            destination.color_intensity[0] = source.light.color.red;
            destination.color_intensity[1] = source.light.color.green;
            destination.color_intensity[2] = source.light.color.blue;
            destination.color_intensity[3] = source.light.intensity;
            destination.parameters[0] = source.light.range;
        }
        if (shadow_view.has_value()) {
            constants.shadow_view_projection = shadow_view->cascades[0].view_projection;
            for (std::size_t cascade_index = 1; cascade_index < runtime_shadow_cascade_count; ++cascade_index) {
                constants.shadow_cascade_view_projections[cascade_index - 1U] =
                    shadow_view->cascades[cascade_index].view_projection;
            }
            for (std::size_t cascade_index = 0; cascade_index < runtime_shadow_cascade_count; ++cascade_index) {
                constants.shadow_cascade_splits[cascade_index] =
                    shadow_view->cascades[cascade_index].split_far;
            }
            constants.shadow_camera_forward[0] = runtime_camera.forward.x;
            constants.shadow_camera_forward[1] = runtime_camera.forward.y;
            constants.shadow_camera_forward[2] = runtime_camera.forward.z;
            constants.shadow_metadata[0] = 1.0F;
            constants.shadow_metadata[1] = static_cast<float>(shadow_view->light_index);
            constants.shadow_metadata[2] = 1.0F / static_cast<float>(runtime_shadow_map_size);
            constants.shadow_metadata[3] = 0.0015F;
        }
        const auto write_environment_metadata = [](float (&metadata)[4],
            const detail::RuntimeEnvironmentBinding& environment, const float weight) {
            if (!environment.available) return;
            metadata[0] = environment.intensity;
            metadata[1] = environment.rotation_radians;
            metadata[2] = environment.max_specular_lod;
            metadata[3] = weight;
        };
        write_environment_metadata(constants.environment_metadata, environment_sources[0], environment_weights[0]);
        write_environment_metadata(constants.environment_secondary_metadata, environment_sources[1], environment_weights[1]);
        write_environment_metadata(constants.environment_tertiary_metadata, environment_sources[2], environment_weights[2]);
        for (std::size_t source_index = 0U; source_index < environment_probe_indices.size(); ++source_index) {
            const auto probe_index = environment_probe_indices[source_index];
            if (!probe_index.has_value() || *probe_index >= reflection_probes.size()) continue;
            const auto& probe = reflection_probes[*probe_index];
            auto& projection = constants.environment_projections[source_index];
            projection.probe_from_model = reflection_probe_from_model(instance, probe);
            projection.half_extents_enabled[0] = probe.probe.half_extents.x * probe.authored.scale.x;
            projection.half_extents_enabled[1] = probe.probe.half_extents.y * probe.authored.scale.y;
            projection.half_extents_enabled[2] = probe.probe.half_extents.z * probe.authored.scale.z;
            projection.half_extents_enabled[3] = 1.0F;
            projection.rotation[0] = probe.authored.rotation.x;
            projection.rotation[1] = probe.authored.rotation.y;
            projection.rotation[2] = probe.authored.rotation.z;
            projection.rotation[3] = probe.authored.rotation.w;
        }
        constants.atmosphere_camera_from_model = runtime_model_matrix_relative_to(
            instance,
            camera_global_position
        );
        constants.atmosphere_sun_direction_intensity[0] = atmosphere.sun_direction.x;
        constants.atmosphere_sun_direction_intensity[1] = atmosphere.sun_direction.y;
        constants.atmosphere_sun_direction_intensity[2] = atmosphere.sun_direction.z;
        constants.atmosphere_sun_direction_intensity[3] = atmosphere.sun_intensity;
        constants.atmosphere_sun_color[0] = atmosphere.sun_color.x;
        constants.atmosphere_sun_color[1] = atmosphere.sun_color.y;
        constants.atmosphere_sun_color[2] = atmosphere.sun_color.z;
        constants.atmosphere_sun_color[3] = 1.0F;
        constants.atmosphere_parameters[0] = atmosphere.settings.rayleigh_strength;
        constants.atmosphere_parameters[1] = atmosphere.settings.mie_strength;
        constants.atmosphere_parameters[2] = atmosphere.settings.mie_anisotropy;
        constants.atmosphere_parameters[3] = atmosphere.settings.aerial_density;
        constants.atmosphere_secondary[0] = atmosphere.settings.sun_angular_radius;
        constants.atmosphere_secondary[1] = atmosphere.settings.horizon_falloff;
        constants.atmosphere_secondary[2] = 1.0F;
        constants.atmosphere_ground_albedo[0] = atmosphere.settings.ground_albedo.x;
        constants.atmosphere_ground_albedo[1] = atmosphere.settings.ground_albedo.y;
        constants.atmosphere_ground_albedo[2] = atmosphere.settings.ground_albedo.z;
        constants.atmosphere_ground_albedo[3] = 1.0F;
        for (std::size_t light_slot = 0U; light_slot < point_shadow_views.size(); ++light_slot) {
            if (!point_shadow_views[light_slot].has_value()) continue;
            const auto& point_view = *point_shadow_views[light_slot];
            auto& point_constants = constants.point_shadows[light_slot];
            point_constants.metadata[0] = 1.0F;
            point_constants.metadata[1] = static_cast<float>(point_view.light_index);
            point_constants.metadata[2] = point_view.range;
            point_constants.metadata[3] = 1.0F / static_cast<float>(runtime_point_shadow_map_size);
            point_constants.model_to_light = runtime_model_matrix_relative_to(
                instance, point_view.global_position
            );
            point_constants.face_view_projections = point_view.face_view_projections;
        }
        if (!constants_iterator->second->write(0, std::as_bytes(std::span{&constants, 1}))) {
            set_diagnostics(diagnostics, "VICE Runtime could not update scene constants.");
            return false;
        }

        if (pipeline == nullptr || !commands->set_graphics_pipeline(*pipeline)) {
            set_diagnostics(diagnostics, "VICE Runtime could not bind a scene material pipeline.");
            return false;
        }

        auto& mesh = state_->meshes.at(instance.mesh);
        if (!commands->set_vertex_buffer(0U, *mesh.vertices) ||
            !commands->set_index_buffer(*mesh.indices, render::IndexFormat::uint32) ||
            !commands->set_graphics_constant_buffer(
                frame_binding,
                *constants_iterator->second,
                0,
                sizeof(SceneConstants)
            )) {
            set_diagnostics(
                diagnostics,
                "VICE Runtime could not bind mesh `" + instance.mesh.value.to_string() + "`."
            );
            return false;
        }
        if (material_state.constants != nullptr &&
            !commands->set_graphics_constant_buffer(
                material_binding,
                *material_state.constants,
                0,
                material_state.constants_size
            )) {
            set_diagnostics(
                diagnostics,
                "VICE Runtime could not bind render material constants for mesh `" +
                    instance.mesh.value.to_string() + "`."
            );
            return false;
        }
        if (material_state.base_color_texture != nullptr &&
            (!commands->set_graphics_texture(
                base_color_texture_binding,
                *material_state.base_color_texture
            ) || material_state.sampler == nullptr ||
            !commands->set_graphics_sampler(material_sampler_binding, *material_state.sampler))) {
            set_diagnostics(
                diagnostics,
                "VICE Runtime could not bind the PBR texture and sampler for mesh `" +
                    instance.mesh.value.to_string() + "`."
            );
            return false;
        }
        if (material_state.normal_texture != nullptr &&
            (!commands->set_graphics_texture(normal_texture_binding, *material_state.normal_texture) ||
                material_state.orm_texture == nullptr ||
                !commands->set_graphics_texture(orm_texture_binding, *material_state.orm_texture) ||
                material_state.emissive_texture == nullptr ||
                !commands->set_graphics_texture(emissive_texture_binding, *material_state.emissive_texture))) {
            set_diagnostics(
                diagnostics,
                "VICE Runtime could not bind V3 normal/ORM/emissive textures for mesh `" +
                    instance.mesh.value.to_string() + "`."
            );
            return false;
        }
        if (material_state.directional_shadow_cascade_count > 0U) {
            const std::size_t cascade_count = std::min<std::size_t>(
                material_state.directional_shadow_cascade_count,
                runtime_shadow_cascade_count
            );
            bool shadows_bound = state_->shadow_sampler != nullptr;
            for (std::size_t cascade_index = 0; cascade_index < cascade_count && shadows_bound; ++cascade_index) {
                shadows_bound = commands->set_graphics_texture(
                    shadow_texture_binding(material_state.shadow_texture_binding_base, cascade_index),
                    *state_->shadow_maps[cascade_index]
                );
            }
            shadows_bound = shadows_bound && commands->set_graphics_sampler(
                shadow_sampler_binding,
                *state_->shadow_sampler
            );
            if (!shadows_bound) {
                set_diagnostics(
                    diagnostics,
                    "VICE Runtime could not bind directional shadow cascades for mesh `" +
                        instance.mesh.value.to_string() + "`."
                );
                return false;
            }
        }
        if (material_state.point_shadow_light_capacity > 0U) {
            const std::size_t point_light_count = std::min<std::size_t>(
                material_state.point_shadow_light_capacity, runtime_point_shadow_light_count
            );
            bool point_shadows_bound = state_->shadow_sampler != nullptr;
            for (std::size_t light_slot = 0U; light_slot < point_light_count && point_shadows_bound; ++light_slot) {
                for (std::size_t face_index = 0U;
                     face_index < runtime_point_shadow_face_count && point_shadows_bound; ++face_index) {
                    point_shadows_bound = commands->set_graphics_texture(
                        point_shadow_texture_binding(
                            material_state.point_shadow_texture_binding_base, light_slot, face_index
                        ),
                        *state_->point_shadow_maps[light_slot][face_index]
                    );
                }
            }
            point_shadows_bound = point_shadows_bound && commands->set_graphics_sampler(
                shadow_sampler_binding, *state_->shadow_sampler
            );
            if (!point_shadows_bound) {
                set_diagnostics(diagnostics,
                    "VICE Runtime could not bind point-light shadow faces for mesh `" +
                    instance.mesh.value.to_string() + "`.");
                return false;
            }
        }
        if (material_state.environment_source_count > 0U) {
            bool environment_bound = true;
            for (std::size_t source_index = 0U;
                 source_index < material_state.environment_source_count && environment_bound;
                 ++source_index) {
                const auto& environment = environment_sources[source_index];
                environment_bound = environment.available && environment.irradiance != nullptr &&
                    environment.specular != nullptr && environment.brdf_lut != nullptr &&
                    commands->set_graphics_texture(environment_texture_binding(source_index, 0U), *environment.irradiance) &&
                    commands->set_graphics_texture(environment_texture_binding(source_index, 1U), *environment.specular) &&
                    commands->set_graphics_texture(environment_texture_binding(source_index, 2U), *environment.brdf_lut);
            }
            environment_bound = environment_bound && environment_sources[0].sampler != nullptr &&
                commands->set_graphics_sampler(environment_sampler_binding, *environment_sources[0].sampler);
            if (!environment_bound) {
                set_diagnostics(diagnostics,
                    "VICE Runtime could not bind reflection environments for mesh `" +
                    instance.mesh.value.to_string() + "`.");
                return false;
            }
        }
        if (!commands->draw_indexed(mesh.index_count)) {
            set_diagnostics(
                diagnostics,
                "VICE Runtime could not encode mesh `" + instance.mesh.value.to_string() + "`."
            );
            return false;
        }
    }

    if (!commands->end_render_pass()) {
        set_diagnostics(diagnostics, "VICE Runtime could not finish the HDR scene render pass.");
        return false;
    }


    // Frames without a camera depth prepass preserve the legacy post-HDR motion timing.
    if (!motion_vectors_encoded && !encode_motion_vectors()) {
        return false;
    }

    const RuntimePostProcessFrame post_process_frame{
        .scene_color = scene_color,
        .previous_scene_color = previous_scene_color,
        .motion_vectors = state_->motion_vectors.get(),
        .depth = current_depth,
        .previous_depth = previous_depth,
        .temporal = temporal_frame,
    };

    render::Texture* working_color = post_process_frame.scene_color;
    bool temporal_resolved = false;
    bool presented = false;
    for (const auto& pass_desc : state_->post_process_plan) {
        if (!pass_desc.enabled) continue;
        switch (pass_desc.pass) {
        case RuntimePostProcessPass::temporal_resolve: {
            if (post_process_frame.scene_color == nullptr ||
                post_process_frame.previous_scene_color == nullptr ||
                post_process_frame.motion_vectors == nullptr || post_process_frame.depth == nullptr ||
                post_process_frame.previous_depth == nullptr || temporal_output == nullptr) {
                set_diagnostics(diagnostics, "VICE Runtime TAA inputs are unavailable.");
                return false;
            }

            const TaaConstants taa_constants{
                .texel_size = {
                    1.0F / static_cast<float>(metrics.drawable_width),
                    1.0F / static_cast<float>(metrics.drawable_height),
                },
                .history_weight = 0.9F,
                .depth_threshold = 0.0025F,
                .history_valid = temporal_frame.history_valid ? 1.0F : 0.0F,
                .motion_rejection = 32.0F,
            };
            if (!state_->taa_constants->write(
                    0U,
                    std::as_bytes(std::span{&taa_constants, 1})
                )) {
                set_diagnostics(diagnostics, "VICE Runtime could not update TAA constants.");
                return false;
            }

            render::RenderPassDesc taa_pass;
            taa_pass.clear_color = render::Color{0.0F, 0.0F, 0.0F, 1.0F};
            if (!commands->begin_render_pass(*temporal_output, taa_pass) ||
                !commands->set_viewport(render::Viewport{
                    .width = static_cast<float>(metrics.drawable_width),
                    .height = static_cast<float>(metrics.drawable_height),
                }) ||
                !commands->set_scissor_rect(render::ScissorRect{
                    .width = metrics.drawable_width,
                    .height = metrics.drawable_height,
                }) ||
                !commands->set_graphics_pipeline(*state_->taa_pipeline) ||
                !commands->set_graphics_constant_buffer(
                    detail::taa_constants_binding,
                    *state_->taa_constants,
                    0U,
                    sizeof(TaaConstants)
                ) ||
                !commands->set_graphics_texture(
                    detail::taa_current_color_binding,
                    *post_process_frame.scene_color
                ) ||
                !commands->set_graphics_texture(
                    detail::taa_history_color_binding,
                    *post_process_frame.previous_scene_color
                ) ||
                !commands->set_graphics_texture(
                    detail::taa_motion_binding,
                    *post_process_frame.motion_vectors
                ) ||
                !commands->set_graphics_texture(
                    detail::taa_current_depth_binding,
                    *post_process_frame.depth
                ) ||
                !commands->set_graphics_texture(
                    detail::taa_previous_depth_binding,
                    *post_process_frame.previous_depth
                ) ||
                !commands->set_graphics_sampler(
                    detail::taa_linear_sampler_binding,
                    *state_->taa_linear_sampler
                ) ||
                !commands->set_graphics_sampler(
                    detail::taa_point_sampler_binding,
                    *state_->taa_point_sampler
                ) ||
                !commands->draw(3U) || !commands->end_render_pass()) {
                set_diagnostics(diagnostics, "VICE Runtime could not execute the TAA resolve pass.");
                return false;
            }
            working_color = temporal_output;
            temporal_resolved = true;
            break;
        }
        case RuntimePostProcessPass::bloom: {
            if (working_color == nullptr || state_->bloom_width == 0U || state_->bloom_height == 0U) {
                set_diagnostics(diagnostics, "VICE Runtime bloom inputs are unavailable.");
                return false;
            }

            render::RenderPassDesc bloom_pass;
            bloom_pass.clear_color = render::Color{0.0F, 0.0F, 0.0F, 1.0F};
            const render::Viewport bloom_viewport{
                .width = static_cast<float>(state_->bloom_width),
                .height = static_cast<float>(state_->bloom_height),
            };
            const render::ScissorRect bloom_scissor{
                .width = state_->bloom_width,
                .height = state_->bloom_height,
            };

            if (!commands->begin_render_pass(*state_->bloom_extract, bloom_pass) ||
                !commands->set_viewport(bloom_viewport) ||
                !commands->set_scissor_rect(bloom_scissor) ||
                !commands->set_graphics_pipeline(*state_->bloom_extract_pipeline) ||
                !commands->set_graphics_constant_buffer(
                    detail::bloom_constants_binding,
                    *state_->bloom_constants[0],
                    0U,
                    sizeof(BloomConstants)
                ) ||
                !commands->set_graphics_texture(detail::bloom_source_binding, *working_color) ||
                !commands->set_graphics_sampler(detail::bloom_sampler_binding, *state_->bloom_sampler) ||
                !commands->draw(3U) || !commands->end_render_pass()) {
                set_diagnostics(diagnostics, "VICE Runtime could not execute bloom highlight extraction.");
                return false;
            }

            if (!commands->begin_render_pass(*state_->bloom_blur, bloom_pass) ||
                !commands->set_viewport(bloom_viewport) ||
                !commands->set_scissor_rect(bloom_scissor) ||
                !commands->set_graphics_pipeline(*state_->bloom_blur_pipeline) ||
                !commands->set_graphics_constant_buffer(
                    detail::bloom_constants_binding,
                    *state_->bloom_constants[1],
                    0U,
                    sizeof(BloomConstants)
                ) ||
                !commands->set_graphics_texture(detail::bloom_source_binding, *state_->bloom_extract) ||
                !commands->set_graphics_sampler(detail::bloom_sampler_binding, *state_->bloom_sampler) ||
                !commands->draw(3U) || !commands->end_render_pass()) {
                set_diagnostics(diagnostics, "VICE Runtime could not execute horizontal bloom blur.");
                return false;
            }

            if (!commands->begin_render_pass(*state_->bloom_extract, bloom_pass) ||
                !commands->set_viewport(bloom_viewport) ||
                !commands->set_scissor_rect(bloom_scissor) ||
                !commands->set_graphics_pipeline(*state_->bloom_blur_pipeline) ||
                !commands->set_graphics_constant_buffer(
                    detail::bloom_constants_binding,
                    *state_->bloom_constants[2],
                    0U,
                    sizeof(BloomConstants)
                ) ||
                !commands->set_graphics_texture(detail::bloom_source_binding, *state_->bloom_blur) ||
                !commands->set_graphics_sampler(detail::bloom_sampler_binding, *state_->bloom_sampler) ||
                !commands->draw(3U) || !commands->end_render_pass()) {
                set_diagnostics(diagnostics, "VICE Runtime could not execute vertical bloom blur.");
                return false;
            }

            render::RenderPassDesc composite_pass;
            composite_pass.clear_color = render::Color{0.0F, 0.0F, 0.0F, 1.0F};
            if (!commands->begin_render_pass(*state_->bloom_composite, composite_pass) ||
                !commands->set_viewport(render::Viewport{
                    .width = static_cast<float>(metrics.drawable_width),
                    .height = static_cast<float>(metrics.drawable_height),
                }) ||
                !commands->set_scissor_rect(render::ScissorRect{
                    .width = metrics.drawable_width,
                    .height = metrics.drawable_height,
                }) ||
                !commands->set_graphics_pipeline(*state_->bloom_composite_pipeline) ||
                !commands->set_graphics_constant_buffer(
                    detail::bloom_constants_binding,
                    *state_->bloom_constants[3],
                    0U,
                    sizeof(BloomConstants)
                ) ||
                !commands->set_graphics_texture(detail::bloom_source_binding, *working_color) ||
                !commands->set_graphics_texture(detail::bloom_texture_binding, *state_->bloom_extract) ||
                !commands->set_graphics_sampler(detail::bloom_sampler_binding, *state_->bloom_sampler) ||
                !commands->draw(3U) || !commands->end_render_pass()) {
                set_diagnostics(diagnostics, "VICE Runtime could not composite HDR bloom.");
                return false;
            }
            working_color = state_->bloom_composite.get();
            break;
        }
        case RuntimePostProcessPass::tone_map: {
            if (presented || working_color == nullptr) {
                set_diagnostics(diagnostics, "VICE Runtime post-process plan has an invalid tone-map pass.");
                return false;
            }
            render::RenderPassDesc presentation_pass;
            presentation_pass.clear_color = render::Color{0.0F, 0.0F, 0.0F, 1.0F};
            if (!commands->begin_render_pass(swapchain, presentation_pass) ||
                !commands->set_viewport(render::Viewport{
                    .width = static_cast<float>(metrics.drawable_width),
                    .height = static_cast<float>(metrics.drawable_height),
                }) ||
                !commands->set_scissor_rect(render::ScissorRect{
                    .width = metrics.drawable_width,
                    .height = metrics.drawable_height,
                }) ||
                !commands->set_graphics_pipeline(*state_->tone_map_pipeline) ||
                !commands->set_graphics_constant_buffer(
                    tone_map_constants_binding,
                    *state_->tone_map_constants,
                    0U,
                    sizeof(ToneMapConstants)
                ) ||
                !commands->set_graphics_texture(
                    tone_map_scene_texture_binding,
                    *working_color
                ) ||
                !commands->set_graphics_sampler(
                    tone_map_sampler_binding,
                    *state_->tone_map_sampler
                ) ||
                !commands->draw(3U) ||
                !commands->end_render_pass() ||
                !commands->present(swapchain)) {
                set_diagnostics(diagnostics, "VICE Runtime could not execute the tone-map presentation pass.");
                return false;
            }
            presented = true;
            break;
        }
        }
    }
    if (!presented) {
        set_diagnostics(diagnostics, "VICE Runtime post-process plan did not present the frame.");
        return false;
    }
    const auto submission = state_->device->submit(std::move(commands));
    if (submission == render::invalid_submission || !state_->device->wait_for_submission(submission)) {
        set_diagnostics(diagnostics, "VICE Runtime scene submission failed.");
        return false;
    }

    const math::Mat4 hiz_relative_view_projection = runtime_apply_projection_jitter(
        math::multiply(
            math::perspective_rh_zo(
                runtime_camera.camera.vertical_fov_radians,
                runtime_camera.aspect_ratio,
                runtime_camera.camera.near_plane,
                runtime_camera.camera.far_plane
            ),
            math::look_at_rh({}, runtime_camera.forward, runtime_camera.up)
        ),
        jitter
    );
    state_->hiz_history.commit(
        runtime_camera.entity,
        camera_global_position,
        runtime_camera.forward,
        runtime_camera.up,
        runtime_camera.camera,
        runtime_camera.aspect_ratio,
        hiz_relative_view_projection,
        instances
    );

    state_->previous_models.clear();
    if (temporal_resolved) {
        state_->previous_models.reserve(instances.size());
        for (const auto& instance : instances) {
            state_->previous_models.emplace(instance.entity, runtime_model_matrix(instance));
        }
        state_->temporal_history.commit_frame(
            runtime_camera.entity,
            unjittered_view_projection,
            view_projection,
            jitter,
            runtime_camera.position,
            runtime_camera.forward
        );
    } else {
        state_->temporal_history.reset();
    }
    state_->history_write_index = 1U - state_->history_write_index;

    if (diagnostics != nullptr) diagnostics->clear();
    return true;
}

} // namespace vice::runtime
