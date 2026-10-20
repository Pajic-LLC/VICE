#pragma once

#include <vice/render/rhi.hpp>

#include <array>
#include <string_view>

namespace vice::runtime::detail {

inline constexpr std::array visibility_resources{
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
        .register_class = render::ShaderRegisterClass::unordered_access,
        .binding = 2U,
        .space = 0U,
        .count = 1U,
        .unbounded = false,
    },
    render::ShaderResourceBindingDesc{
        .register_class = render::ShaderRegisterClass::unordered_access,
        .binding = 3U,
        .space = 0U,
        .count = 1U,
        .unbounded = false,
    },
};

inline constexpr render::ShaderBindingKey visibility_constants_binding{
    .register_class = render::ShaderRegisterClass::constant_buffer,
    .binding = 0U,
    .space = 0U,
    .array_element = 0U,
};

inline constexpr render::ShaderBindingKey visibility_input_binding{
    .register_class = render::ShaderRegisterClass::shader_resource,
    .binding = 1U,
    .space = 0U,
    .array_element = 0U,
};

inline constexpr render::ShaderBindingKey visibility_count_binding{
    .register_class = render::ShaderRegisterClass::unordered_access,
    .binding = 2U,
    .space = 0U,
    .array_element = 0U,
};

inline constexpr render::ShaderBindingKey visibility_indices_binding{
    .register_class = render::ShaderRegisterClass::unordered_access,
    .binding = 3U,
    .space = 0U,
    .array_element = 0U,
};

inline constexpr std::string_view visibility_hlsl_source = R"(
cbuffer VisibilityFrame : register(b0) {
    float4 frustum_planes[6];
    uint instance_count;
    uint visibility_padding_0;
    uint visibility_padding_1;
    uint visibility_padding_2;
};

struct VisibilityInput {
    float4 center_radius;
    uint instance_index;
    uint padding_0;
    uint padding_1;
    uint padding_2;
};

StructuredBuffer<VisibilityInput> visibility_instances : register(t1);
RWStructuredBuffer<uint> visible_count : register(u2);
RWStructuredBuffer<uint> visible_indices : register(u3);

[numthreads(64, 1, 1)]
void vice_runtime_visibility_cs(uint3 dispatch_id : SV_DispatchThreadID) {
    const uint input_index = dispatch_id.x;
    if (input_index >= instance_count) return;

    const VisibilityInput item = visibility_instances[input_index];
    bool visible = true;
    [unroll]
    for (uint plane_index = 0U; plane_index < 6U; ++plane_index) {
        const float distance = dot(
            frustum_planes[plane_index].xyz,
            item.center_radius.xyz
        ) + frustum_planes[plane_index].w;
        visible = visible && distance >= -item.center_radius.w;
    }
    if (!visible) return;

    uint output_index = 0U;
    InterlockedAdd(visible_count[0], 1U, output_index);
    visible_indices[output_index] = item.instance_index;
}
)";

inline constexpr std::string_view visibility_metal_source = R"(
#include <metal_stdlib>
using namespace metal;

struct VisibilityFrame {
    float4 frustum_planes[6];
    uint instance_count;
    uint padding_0;
    uint padding_1;
    uint padding_2;
};

struct VisibilityInput {
    float4 center_radius;
    uint instance_index;
    uint padding_0;
    uint padding_1;
    uint padding_2;
};

kernel void vice_runtime_visibility_cs(
    constant VisibilityFrame& frame [[buffer(0)]],
    device const VisibilityInput* visibility_instances [[buffer(1)]],
    device atomic_uint* visible_count [[buffer(2)]],
    device uint* visible_indices [[buffer(3)]],
    uint3 dispatch_id [[thread_position_in_grid]]) {
    const uint input_index = dispatch_id.x;
    if (input_index >= frame.instance_count) return;

    const VisibilityInput item = visibility_instances[input_index];
    bool visible = true;
    for (uint plane_index = 0U; plane_index < 6U; ++plane_index) {
        const float distance = dot(
            frame.frustum_planes[plane_index].xyz,
            item.center_radius.xyz
        ) + frame.frustum_planes[plane_index].w;
        visible = visible && distance >= -item.center_radius.w;
    }
    if (!visible) return;

    const uint output_index = atomic_fetch_add_explicit(
        &visible_count[0],
        1U,
        memory_order_relaxed
    );
    visible_indices[output_index] = item.instance_index;
}
)";

} // namespace vice::runtime::detail
