#pragma once
#include <string_view>
namespace vice::runtime::detail {
inline constexpr std::string_view gpu_shadow_hlsl_source = R"(
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

inline constexpr std::string_view gpu_shadow_metal_source = R"(
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
    // Match the color/motion shaders and HLSL: transform the vertex to world
    // first. (VP * model) * vertex rounds differently from VP * (model * vertex)
    // and can reject visible color/motion fragments against prepass depth.
    const float4 world_position = instance_models[scene_instance] * float4(input.position, 1.0);
    return frame.view_projection * world_position;
}
)";

}
