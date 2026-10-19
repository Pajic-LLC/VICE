#include <vice/runtime/scene.hpp>

#include <vice/runtime/post_process.hpp>

#include "bloom_shaders.hpp"
#include "material_pipeline.hpp"
#include "taa_shaders.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
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
};

constexpr render::ShaderBindingKey frame_binding{
    .register_class = render::ShaderRegisterClass::constant_buffer,
    .binding = 0,
    .space = 0,
    .array_element = 0,
};

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

constexpr render::ShaderBindingKey shadow_sampler_binding{
    .register_class = render::ShaderRegisterClass::sampler,
    .binding = 1,
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
};

struct ShadowConstants {
    math::Mat4 view_projection{};
    math::Mat4 model{};
};

struct MotionConstants {
    math::Mat4 current_view_projection{};
    math::Mat4 previous_view_projection{};
    math::Mat4 current_model{};
    math::Mat4 previous_model{};
};

struct TaaConstants {
    float texel_size[2]{};
    float history_weight = 0.9F;
    float depth_threshold = 0.0025F;
    float history_valid = 0.0F;
    float motion_rejection = 32.0F;
    float padding[2]{};
};

static_assert(sizeof(ToneMapConstants) == 16U);
static_assert(sizeof(BloomConstants) == 32U);
static_assert(sizeof(SceneLightConstants) == 48U);
static_assert(sizeof(SceneConstants) == 848U);
static_assert(sizeof(ShadowConstants) == 128U);
static_assert(sizeof(MotionConstants) == 256U);
static_assert(sizeof(TaaConstants) == 32U);

constexpr std::string_view hlsl_source = R"(
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

constexpr std::string_view metal_source = R"(
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

constexpr std::string_view shadow_hlsl_source = R"(
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

constexpr std::string_view motion_hlsl_source = R"(
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

[[nodiscard]] std::unique_ptr<render::Buffer> create_constant_buffer(render::Device& device) {
    render::BufferDesc desc;
    desc.size = sizeof(SceneConstants);
    desc.usage = render::BufferUsage::uniform;
    desc.memory = render::MemoryLocation::cpu_to_gpu;
    desc.debug_name = "VICE Runtime scene constants";
    return device.create_buffer(desc);
}

[[nodiscard]] std::unique_ptr<render::Buffer> create_shadow_constant_buffer(
    render::Device& device
) {
    render::BufferDesc desc;
    desc.size = sizeof(ShadowConstants);
    desc.usage = render::BufferUsage::uniform;
    desc.memory = render::MemoryLocation::cpu_to_gpu;
    desc.debug_name = "VICE Runtime shadow constants";
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
    float split_near = std::max(0.001F, camera.camera.near_plane);
    for (std::size_t index = 0; index < result.cascades.size(); ++index) {
        result.cascades[index] = fitted_shadow_cascade(
            camera,
            light,
            split_near,
            splits[index]
        );
        split_near = splits[index];
    }
    return result;
}

math::Mat4 runtime_model_matrix(const RenderMeshInstance& instance) noexcept {
    auto result = math::translation({
        static_cast<float>(instance.spatial.global_position.x),
        static_cast<float>(instance.spatial.global_position.y),
        static_cast<float>(instance.spatial.global_position.z),
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
    };

    render::Device* device = nullptr;
    CookedMeshCatalog catalog;
    detail::RuntimeMaterialLibrary materials;
    std::unique_ptr<render::ShaderModule> vertex_shader;
    std::unique_ptr<render::ShaderModule> fragment_shader;
    std::unique_ptr<render::GraphicsPipeline> pipeline;
    std::unique_ptr<render::ShaderModule> motion_vertex_shader;
    std::unique_ptr<render::ShaderModule> motion_fragment_shader;
    std::unique_ptr<render::GraphicsPipeline> motion_pipeline;
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
    std::unique_ptr<render::ShaderModule> shadow_vertex_shader;
    std::unique_ptr<render::GraphicsPipeline> shadow_pipeline;
    std::array<std::unique_ptr<render::Texture>, runtime_shadow_cascade_count> shadow_maps;
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
    std::unordered_map<world::PersistentId, std::unique_ptr<render::Buffer>, world::PersistentIdHash> motion_constants;
    std::unordered_map<world::PersistentId, math::Mat4, world::PersistentIdHash> previous_models;
    std::array<
        std::unordered_map<world::PersistentId, std::unique_ptr<render::Buffer>, world::PersistentIdHash>,
        runtime_shadow_cascade_count
    > shadow_constants;
    std::uint32_t depth_width = 0;
    std::uint32_t depth_height = 0;
    std::uint32_t bloom_width = 0;
    std::uint32_t bloom_height = 0;
    RuntimeTemporalHistory temporal_history;
    RuntimePostProcessPlan post_process_plan = default_runtime_post_process_plan();
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
    if (!state_->materials.initialize(
            device,
            render::TextureFormat::rgba16_float,
            mesh_layouts,
            content_root,
            diagnostics
        )) {
        return false;
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
    pipeline_desc.depth.compare = render::CompareOp::less;
    pipeline_desc.vertex_buffers = mesh_layouts;
    pipeline_desc.debug_name = "VICE Runtime mesh fallback pipeline";
    state_->pipeline = device.create_graphics_pipeline(pipeline_desc);
    if (!state_->pipeline) {
        set_diagnostics(diagnostics, "VICE Runtime could not create the fallback scene graphics pipeline.");
        return false;
    }

    render::ShaderModuleDesc motion_vertex_desc;
    motion_vertex_desc.stage = render::ShaderStage::vertex;
    motion_vertex_desc.language = vertex_desc.language;
    motion_vertex_desc.source = metal ? motion_metal_source : motion_hlsl_source;
    motion_vertex_desc.resources = vertex_resources;
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
    if (!state_->motion_pipeline) {
        set_diagnostics(diagnostics, "VICE Runtime could not create the motion-vector pipeline.");
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

    render::ShaderModuleDesc shadow_vertex_desc;
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

    for (const auto& record : state_->catalog.records()) {
        auto vertices = create_buffer_with_data(
            device,
            render::BufferUsage::vertex,
            std::as_bytes(std::span{record.asset.vertices}),
            "VICE Runtime mesh vertices"
        );
        auto indices = create_buffer_with_data(
            device,
            render::BufferUsage::index,
            std::as_bytes(std::span{record.asset.indices}),
            "VICE Runtime mesh indices"
        );
        if (!vertices || !indices) {
            set_diagnostics(
                diagnostics,
                "VICE Runtime could not upload cooked mesh `" + record.path.generic_string() + "`."
            );
            return false;
        }
        state_->meshes.emplace(
            record.resource_id,
            State::MeshGpu{
                .vertices = std::move(vertices),
                .indices = std::move(indices),
                .index_count = static_cast<std::uint32_t>(record.asset.indices.size()),
            }
        );
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

    state_->scene_color = std::move(scene_color);
    state_->temporal_color_history = std::move(temporal_color_history);
    state_->depth_history = std::move(depth_history);
    state_->motion_vectors = std::move(motion_vectors);
    state_->bloom_extract = std::move(bloom_extract);
    state_->bloom_blur = std::move(bloom_blur);
    state_->bloom_composite = std::move(bloom_composite);
    state_->previous_models.clear();
    state_->temporal_history.reset();
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

bool RuntimeSceneRenderer::render(
    const world::World& world,
    render::Swapchain& swapchain,
    const platform::WindowMetrics metrics,
    const render::Color clear_color,
    std::string* const diagnostics
) {
    if (state_ == nullptr || !state_->initialized || state_->device == nullptr ||
        !state_->pipeline || !state_->motion_pipeline || !state_->taa_pipeline ||
        !state_->taa_linear_sampler || !state_->taa_point_sampler || !state_->taa_constants ||
        !state_->bloom_extract_pipeline || !state_->bloom_blur_pipeline ||
        !state_->bloom_composite_pipeline || !state_->bloom_sampler ||
        !state_->tone_map_pipeline || !state_->tone_map_sampler || !state_->tone_map_constants) {
        set_diagnostics(diagnostics, "Runtime scene renderer is not initialized.");
        return false;
    }
    if (metrics.drawable_width == 0U || metrics.drawable_height == 0U) {
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

    const auto instances = collect_render_mesh_instances(world);
    for (const auto& instance : instances) {
        if (!state_->meshes.contains(instance.mesh)) {
            set_diagnostics(
                diagnostics,
                "Runtime World references missing cooked mesh `" + instance.mesh.value.to_string() + "`."
            );
            return false;
        }
        if (instance.material.has_value() &&
            state_->materials.find(*instance.material).pipeline == nullptr) {
            set_diagnostics(
                diagnostics,
                "Runtime World references missing cooked render material `" +
                    instance.material->value.to_string() + "`."
            );
            return false;
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
    auto* const previous_depth = temporal_frame.history_valid
        ? state_->depth_history[1U - state_->history_write_index].get()
        : current_depth;

    const auto authored_lights = collect_runtime_lights(world);
    const auto shadow_view = directional_shadow_view(runtime_camera, authored_lights);
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

    auto commands = state_->device->create_command_list();
    if (!commands) {
        set_diagnostics(diagnostics, "VICE Runtime could not create scene commands.");
        return false;
    }

    if (shadow_view.has_value()) {
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

    render::RenderPassDesc pass;
    pass.clear_color = clear_color;
    pass.depth_attachment = current_depth;
    pass.clear_depth = 1.0F;
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

    for (const auto& instance : instances) {
        auto constants_iterator = state_->constants.find(instance.entity);
        if (constants_iterator == state_->constants.end()) {
            auto constants = create_constant_buffer(*state_->device);
            if (!constants) {
                set_diagnostics(diagnostics, "VICE Runtime could not create scene constants.");
                return false;
            }
            constants_iterator = state_->constants.emplace(instance.entity, std::move(constants)).first;
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
        if (!constants_iterator->second->write(0, std::as_bytes(std::span{&constants, 1}))) {
            set_diagnostics(diagnostics, "VICE Runtime could not update scene constants.");
            return false;
        }

        render::GraphicsPipeline* pipeline = state_->pipeline.get();
        detail::RuntimeMaterialBinding material_state{};
        if (instance.material.has_value()) {
            material_state = state_->materials.find(*instance.material);
            pipeline = material_state.pipeline;
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
