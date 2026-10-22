#pragma once

#include <vice/render/rhi.hpp>

#include <array>
#include <string_view>

namespace vice::runtime::detail {

inline constexpr std::array hiz_resources{
    render::ShaderResourceBindingDesc{
        .register_class = render::ShaderRegisterClass::constant_buffer,
        .binding = 0U,
        .space = 0U,
        .count = 1U,
        .unbounded = false,
    },
    render::ShaderResourceBindingDesc{
        .register_class = render::ShaderRegisterClass::shader_resource,
        .binding = 0U,
        .space = 0U,
        .count = 1U,
        .unbounded = false,
    },
    render::ShaderResourceBindingDesc{
        .register_class = render::ShaderRegisterClass::unordered_access,
        .binding = 1U,
        .space = 0U,
        .count = 1U,
        .unbounded = false,
    },
    render::ShaderResourceBindingDesc{
        .register_class = render::ShaderRegisterClass::sampler,
        .binding = 0U,
        .space = 0U,
        .count = 1U,
        .unbounded = false,
    },
};

inline constexpr render::ShaderBindingKey hiz_constants_binding{
    .register_class = render::ShaderRegisterClass::constant_buffer,
    .binding = 0U,
    .space = 0U,
    .array_element = 0U,
};

inline constexpr render::ShaderBindingKey hiz_source_binding{
    .register_class = render::ShaderRegisterClass::shader_resource,
    .binding = 0U,
    .space = 0U,
    .array_element = 0U,
};

inline constexpr render::ShaderBindingKey hiz_output_binding{
    .register_class = render::ShaderRegisterClass::unordered_access,
    .binding = 1U,
    .space = 0U,
    .array_element = 0U,
};

inline constexpr render::ShaderBindingKey hiz_sampler_binding{
    .register_class = render::ShaderRegisterClass::sampler,
    .binding = 0U,
    .space = 0U,
    .array_element = 0U,
};

inline constexpr std::string_view hiz_copy_hlsl_source = R"(
cbuffer HiZFrame : register(b0) {
    uint2 source_size;
    uint2 target_size;
};
Texture2D<float> source_depth : register(t0);
RWTexture2D<float> output_depth : register(u1);
SamplerState hiz_sampler : register(s0);

[numthreads(8, 8, 1)]
void vice_runtime_hiz_copy_cs(uint3 dispatch_id : SV_DispatchThreadID) {
    const uint2 target = dispatch_id.xy;
    if (target.x >= target_size.x || target.y >= target_size.y) return;
    const float2 uv = (float2(target) + 0.5) / float2(source_size);
    output_depth[target] = source_depth.SampleLevel(hiz_sampler, uv, 0.0);
}
)";

inline constexpr std::string_view hiz_reduce_hlsl_source = R"(
cbuffer HiZFrame : register(b0) {
    uint2 source_size;
    uint2 target_size;
};
Texture2D<float> source_depth : register(t0);
RWTexture2D<float> output_depth : register(u1);
SamplerState hiz_sampler : register(s0);

float hiz_sample(uint2 coordinate) {
    coordinate = min(coordinate, source_size - uint2(1, 1));
    const float2 uv = (float2(coordinate) + 0.5) / float2(source_size);
    return source_depth.SampleLevel(hiz_sampler, uv, 0.0);
}

[numthreads(8, 8, 1)]
void vice_runtime_hiz_reduce_cs(uint3 dispatch_id : SV_DispatchThreadID) {
    const uint2 target = dispatch_id.xy;
    if (target.x >= target_size.x || target.y >= target_size.y) return;
    const uint2 source = target * 2U;
    const float depth_00 = hiz_sample(source);
    const float depth_10 = hiz_sample(source + uint2(1, 0));
    const float depth_01 = hiz_sample(source + uint2(0, 1));
    const float depth_11 = hiz_sample(source + uint2(1, 1));
    output_depth[target] = max(max(depth_00, depth_10), max(depth_01, depth_11));
}
)";

inline constexpr std::string_view hiz_copy_metal_source = R"(
#include <metal_stdlib>
using namespace metal;

struct HiZFrame {
    uint2 source_size;
    uint2 target_size;
};

kernel void vice_runtime_hiz_copy_cs(
    constant HiZFrame& frame [[buffer(0)]],
    depth2d<float> source_depth [[texture(0)]],
    texture2d<float, access::write> output_depth [[texture(1)]],
    sampler hiz_sampler [[sampler(0)]],
    uint3 dispatch_id [[thread_position_in_grid]]) {
    const uint2 target = dispatch_id.xy;
    if (target.x >= frame.target_size.x || target.y >= frame.target_size.y) return;
    const float2 uv = (float2(target) + 0.5) / float2(frame.source_size);
    output_depth.write(source_depth.sample(hiz_sampler, uv), target);
}
)";

inline constexpr std::string_view hiz_reduce_metal_source = R"(
#include <metal_stdlib>
using namespace metal;

struct HiZFrame {
    uint2 source_size;
    uint2 target_size;
};

float vice_hiz_sample(
    texture2d<float> source_depth,
    sampler hiz_sampler,
    uint2 source_size,
    uint2 coordinate) {
    coordinate = min(coordinate, source_size - uint2(1));
    const float2 uv = (float2(coordinate) + 0.5) / float2(source_size);
    return source_depth.sample(hiz_sampler, uv).r;
}

kernel void vice_runtime_hiz_reduce_cs(
    constant HiZFrame& frame [[buffer(0)]],
    texture2d<float> source_depth [[texture(0)]],
    texture2d<float, access::write> output_depth [[texture(1)]],
    sampler hiz_sampler [[sampler(0)]],
    uint3 dispatch_id [[thread_position_in_grid]]) {
    const uint2 target = dispatch_id.xy;
    if (target.x >= frame.target_size.x || target.y >= frame.target_size.y) return;
    const uint2 source = target * 2U;
    const float depth_00 = vice_hiz_sample(source_depth, hiz_sampler, frame.source_size, source);
    const float depth_10 = vice_hiz_sample(source_depth, hiz_sampler, frame.source_size, source + uint2(1, 0));
    const float depth_01 = vice_hiz_sample(source_depth, hiz_sampler, frame.source_size, source + uint2(0, 1));
    const float depth_11 = vice_hiz_sample(source_depth, hiz_sampler, frame.source_size, source + uint2(1, 1));
    output_depth.write(max(max(depth_00, depth_10), max(depth_01, depth_11)), target);
}
)";

} // namespace vice::runtime::detail
