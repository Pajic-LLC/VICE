#pragma once

#include <vice/render/rhi.hpp>

#include <array>
#include <string_view>

namespace vice::runtime::detail {

constexpr render::ShaderBindingKey bloom_constants_binding{
    .register_class = render::ShaderRegisterClass::constant_buffer,
    .binding = 0,
    .space = 0,
    .array_element = 0,
};

constexpr render::ShaderBindingKey bloom_source_binding{
    .register_class = render::ShaderRegisterClass::shader_resource,
    .binding = 0,
    .space = 0,
    .array_element = 0,
};

constexpr render::ShaderBindingKey bloom_texture_binding{
    .register_class = render::ShaderRegisterClass::shader_resource,
    .binding = 1,
    .space = 0,
    .array_element = 0,
};

constexpr render::ShaderBindingKey bloom_sampler_binding{
    .register_class = render::ShaderRegisterClass::sampler,
    .binding = 0,
    .space = 0,
    .array_element = 0,
};

static constexpr std::array bloom_single_fragment_resources{
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

static constexpr std::array bloom_composite_fragment_resources{
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
        .register_class = render::ShaderRegisterClass::shader_resource,
        .binding = 1,
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

inline constexpr std::string_view bloom_hlsl_source = R"(
cbuffer BloomFrame : register(b0) {
    float4 bloom_data0;
    float4 bloom_data1;
};
Texture2D<float4> source_color : register(t0);
Texture2D<float4> bloom_color : register(t1);
SamplerState bloom_sampler : register(s0);

struct BloomVertexOut {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

BloomVertexOut vice_runtime_bloom_vs(uint vertex_id : SV_VertexID) {
    const float2 uvs[3] = {
        float2(0.0, 0.0),
        float2(2.0, 0.0),
        float2(0.0, 2.0)
    };
    BloomVertexOut output;
    output.uv = uvs[vertex_id];
    output.position = float4(
        output.uv.x * 2.0 - 1.0,
        1.0 - output.uv.y * 2.0,
        0.0,
        1.0
    );
    return output;
}

float3 vice_bloom_extract(float3 color) {
    color = max(color, 0.0);
    const float brightness = max(color.r, max(color.g, color.b));
    const float threshold = max(bloom_data0.z, 0.0);
    const float knee = max(threshold * saturate(bloom_data0.w), 0.00001);
    float soft = brightness - threshold + knee;
    soft = clamp(soft, 0.0, 2.0 * knee);
    soft = (soft * soft) / (4.0 * knee + 0.00001);
    const float contribution = max(brightness - threshold, soft);
    return color * (contribution / max(brightness, 0.00001));
}

float4 vice_runtime_bloom_extract_fs(BloomVertexOut input) : SV_Target0 {
    return float4(vice_bloom_extract(source_color.Sample(bloom_sampler, input.uv).rgb), 1.0);
}

float4 vice_runtime_bloom_blur_fs(BloomVertexOut input) : SV_Target0 {
    const float2 direction = bloom_data1.yz;
    const float2 texel = bloom_data0.xy * direction;
    float3 color = source_color.Sample(bloom_sampler, input.uv).rgb * 0.2270270270;
    color += source_color.Sample(bloom_sampler, input.uv + texel * 1.3846153846).rgb * 0.3162162162;
    color += source_color.Sample(bloom_sampler, input.uv - texel * 1.3846153846).rgb * 0.3162162162;
    color += source_color.Sample(bloom_sampler, input.uv + texel * 3.2307692308).rgb * 0.0702702703;
    color += source_color.Sample(bloom_sampler, input.uv - texel * 3.2307692308).rgb * 0.0702702703;
    return float4(max(color, 0.0), 1.0);
}

float4 vice_runtime_bloom_composite_fs(BloomVertexOut input) : SV_Target0 {
    const float3 scene = max(source_color.Sample(bloom_sampler, input.uv).rgb, 0.0);
    const float3 bloom = max(bloom_color.Sample(bloom_sampler, input.uv).rgb, 0.0);
    return float4(scene + bloom * max(bloom_data1.x, 0.0), 1.0);
}
)";

inline constexpr std::string_view bloom_metal_source = R"(
#include <metal_stdlib>
using namespace metal;

struct BloomFrame {
    float4 bloom_data0;
    float4 bloom_data1;
};

struct BloomVertexOut {
    float4 position [[position]];
    float2 uv;
};

vertex BloomVertexOut vice_runtime_bloom_vs(uint vertex_id [[vertex_id]]) {
    constexpr float2 uvs[3] = {
        float2(0.0, 0.0),
        float2(2.0, 0.0),
        float2(0.0, 2.0)
    };
    BloomVertexOut output;
    output.uv = uvs[vertex_id];
    output.position = float4(
        output.uv.x * 2.0 - 1.0,
        1.0 - output.uv.y * 2.0,
        0.0,
        1.0
    );
    return output;
}

float3 vice_bloom_extract(float3 color, constant BloomFrame& frame) {
    color = max(color, float3(0.0));
    const float brightness = max(color.r, max(color.g, color.b));
    const float threshold = max(frame.bloom_data0.z, 0.0);
    const float knee = max(threshold * clamp(frame.bloom_data0.w, 0.0, 1.0), 0.00001);
    float soft = brightness - threshold + knee;
    soft = clamp(soft, 0.0, 2.0 * knee);
    soft = (soft * soft) / (4.0 * knee + 0.00001);
    const float contribution = max(brightness - threshold, soft);
    return color * (contribution / max(brightness, 0.00001));
}

fragment float4 vice_runtime_bloom_extract_fs(
    BloomVertexOut input [[stage_in]],
    constant BloomFrame& frame [[buffer(0)]],
    texture2d<float> source_color [[texture(0)]],
    sampler bloom_sampler [[sampler(0)]]) {
    return float4(vice_bloom_extract(source_color.sample(bloom_sampler, input.uv).rgb, frame), 1.0);
}

fragment float4 vice_runtime_bloom_blur_fs(
    BloomVertexOut input [[stage_in]],
    constant BloomFrame& frame [[buffer(0)]],
    texture2d<float> source_color [[texture(0)]],
    sampler bloom_sampler [[sampler(0)]]) {
    const float2 direction = frame.bloom_data1.yz;
    const float2 texel = frame.bloom_data0.xy * direction;
    float3 color = source_color.sample(bloom_sampler, input.uv).rgb * 0.2270270270;
    color += source_color.sample(bloom_sampler, input.uv + texel * 1.3846153846).rgb * 0.3162162162;
    color += source_color.sample(bloom_sampler, input.uv - texel * 1.3846153846).rgb * 0.3162162162;
    color += source_color.sample(bloom_sampler, input.uv + texel * 3.2307692308).rgb * 0.0702702703;
    color += source_color.sample(bloom_sampler, input.uv - texel * 3.2307692308).rgb * 0.0702702703;
    return float4(max(color, float3(0.0)), 1.0);
}

fragment float4 vice_runtime_bloom_composite_fs(
    BloomVertexOut input [[stage_in]],
    constant BloomFrame& frame [[buffer(0)]],
    texture2d<float> source_color [[texture(0)]],
    texture2d<float> bloom_color [[texture(1)]],
    sampler bloom_sampler [[sampler(0)]]) {
    const float3 scene = max(source_color.sample(bloom_sampler, input.uv).rgb, float3(0.0));
    const float3 bloom = max(bloom_color.sample(bloom_sampler, input.uv).rgb, float3(0.0));
    return float4(scene + bloom * max(frame.bloom_data1.x, 0.0), 1.0);
}
)";

} // namespace vice::runtime::detail
