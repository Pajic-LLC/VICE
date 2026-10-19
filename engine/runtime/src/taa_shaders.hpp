#pragma once

#include <vice/render/rhi.hpp>

#include <array>
#include <string_view>

namespace vice::runtime::detail {

inline constexpr render::ShaderBindingKey taa_constants_binding{
    .register_class = render::ShaderRegisterClass::constant_buffer,
    .binding = 0,
    .space = 0,
    .array_element = 0,
};

inline constexpr render::ShaderBindingKey taa_current_color_binding{
    .register_class = render::ShaderRegisterClass::shader_resource,
    .binding = 0,
    .space = 0,
    .array_element = 0,
};
inline constexpr render::ShaderBindingKey taa_history_color_binding{
    .register_class = render::ShaderRegisterClass::shader_resource,
    .binding = 1,
    .space = 0,
    .array_element = 0,
};
inline constexpr render::ShaderBindingKey taa_motion_binding{
    .register_class = render::ShaderRegisterClass::shader_resource,
    .binding = 2,
    .space = 0,
    .array_element = 0,
};
inline constexpr render::ShaderBindingKey taa_current_depth_binding{
    .register_class = render::ShaderRegisterClass::shader_resource,
    .binding = 3,
    .space = 0,
    .array_element = 0,
};
inline constexpr render::ShaderBindingKey taa_previous_depth_binding{
    .register_class = render::ShaderRegisterClass::shader_resource,
    .binding = 4,
    .space = 0,
    .array_element = 0,
};
inline constexpr render::ShaderBindingKey taa_linear_sampler_binding{
    .register_class = render::ShaderRegisterClass::sampler,
    .binding = 0,
    .space = 0,
    .array_element = 0,
};
inline constexpr render::ShaderBindingKey taa_point_sampler_binding{
    .register_class = render::ShaderRegisterClass::sampler,
    .binding = 1,
    .space = 0,
    .array_element = 0,
};

inline constexpr std::array taa_fragment_resources{
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
    render::ShaderResourceBindingDesc{
        .register_class = render::ShaderRegisterClass::shader_resource,
        .binding = 4,
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
    render::ShaderResourceBindingDesc{
        .register_class = render::ShaderRegisterClass::sampler,
        .binding = 1,
        .space = 0,
        .count = 1,
        .unbounded = false,
    },
};

inline constexpr std::string_view taa_hlsl_source = R"(
cbuffer TaaFrame : register(b0) {
    float4 taa_data0;
    float4 taa_data1;
};
Texture2D<float4> current_color : register(t0);
Texture2D<float4> history_color : register(t1);
Texture2D<float4> motion_vectors : register(t2);
Texture2D<float> current_depth : register(t3);
Texture2D<float> previous_depth : register(t4);
SamplerState linear_sampler : register(s0);
SamplerState point_sampler : register(s1);

struct TaaVertexOut {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

TaaVertexOut vice_runtime_taa_vs(uint vertex_id : SV_VertexID) {
    const float2 uvs[3] = {
        float2(0.0, 0.0),
        float2(2.0, 0.0),
        float2(0.0, 2.0)
    };
    TaaVertexOut output;
    output.uv = uvs[vertex_id];
    output.position = float4(
        output.uv.x * 2.0 - 1.0,
        1.0 - output.uv.y * 2.0,
        0.0,
        1.0
    );
    return output;
}

float4 vice_runtime_taa_fs(TaaVertexOut input) : SV_Target0 {
    const float2 texel = taa_data0.xy;
    const float base_history_weight = taa_data0.z;
    const float depth_threshold = taa_data0.w;
    const bool history_valid = taa_data1.x > 0.5;
    const float motion_rejection = taa_data1.y;
    const float2 uv = input.uv;
    const float4 current = current_color.SampleLevel(linear_sampler, uv, 0.0);
    if (!history_valid) return current;

    const float2 velocity = motion_vectors.SampleLevel(point_sampler, uv, 0.0).xy;
    const float2 history_uv = uv - velocity;
    if (history_uv.x < 0.0 || history_uv.x > 1.0 ||
        history_uv.y < 0.0 || history_uv.y > 1.0) {
        return current;
    }

    const float current_z = current_depth.SampleLevel(point_sampler, uv, 0.0);
    const float previous_z = previous_depth.SampleLevel(point_sampler, history_uv, 0.0);
    const float depth_delta = abs(current_z - previous_z);
    const float relative_depth_threshold = max(
        depth_threshold,
        depth_threshold * 8.0 * max(abs(current_z), abs(previous_z))
    );
    if (depth_delta > relative_depth_threshold) return current;

    float3 neighborhood_min = current.rgb;
    float3 neighborhood_max = current.rgb;
    [unroll]
    for (int y = -1; y <= 1; ++y) {
        [unroll]
        for (int x = -1; x <= 1; ++x) {
            const float2 sample_uv = uv + float2((float)x, (float)y) * texel;
            const float3 sample_color = current_color.SampleLevel(
                point_sampler,
                sample_uv,
                0.0
            ).rgb;
            neighborhood_min = min(neighborhood_min, sample_color);
            neighborhood_max = max(neighborhood_max, sample_color);
        }
    }

    float4 history = history_color.SampleLevel(linear_sampler, history_uv, 0.0);
    history.rgb = clamp(history.rgb, neighborhood_min, neighborhood_max);
    const float motion_factor = saturate(1.0 - length(velocity) * motion_rejection);
    const float history_weight = base_history_weight * motion_factor;
    return float4(lerp(current.rgb, history.rgb, history_weight), current.a);
}
)";

inline constexpr std::string_view taa_metal_source = R"(
#include <metal_stdlib>
using namespace metal;

struct TaaFrame {
    float4 taa_data0;
    float4 taa_data1;
};
struct TaaVertexOut {
    float4 position [[position]];
    float2 uv;
};

vertex TaaVertexOut vice_runtime_taa_vs(uint vertex_id [[vertex_id]]) {
    constexpr float2 uvs[3] = {
        float2(0.0, 0.0),
        float2(2.0, 0.0),
        float2(0.0, 2.0)
    };
    TaaVertexOut output;
    output.uv = uvs[vertex_id];
    output.position = float4(
        output.uv.x * 2.0 - 1.0,
        1.0 - output.uv.y * 2.0,
        0.0,
        1.0
    );
    return output;
}

fragment float4 vice_runtime_taa_fs(
    TaaVertexOut input [[stage_in]],
    constant TaaFrame& frame [[buffer(0)]],
    texture2d<float> current_color [[texture(0)]],
    texture2d<float> history_color [[texture(1)]],
    texture2d<float> motion_vectors [[texture(2)]],
    depth2d<float> current_depth [[texture(3)]],
    depth2d<float> previous_depth [[texture(4)]],
    sampler linear_sampler [[sampler(0)]],
    sampler point_sampler [[sampler(1)]]) {
    const float2 texel = frame.taa_data0.xy;
    const float base_history_weight = frame.taa_data0.z;
    const float depth_threshold = frame.taa_data0.w;
    const bool history_valid = frame.taa_data1.x > 0.5;
    const float motion_rejection = frame.taa_data1.y;
    const float2 uv = input.uv;
    const float4 current = current_color.sample(linear_sampler, uv);
    if (!history_valid) return current;

    const float2 velocity = motion_vectors.sample(point_sampler, uv).xy;
    const float2 history_uv = uv - velocity;
    if (history_uv.x < 0.0 || history_uv.x > 1.0 ||
        history_uv.y < 0.0 || history_uv.y > 1.0) {
        return current;
    }

    const float current_z = current_depth.sample(point_sampler, uv);
    const float previous_z = previous_depth.sample(point_sampler, history_uv);
    const float depth_delta = abs(current_z - previous_z);
    const float relative_depth_threshold = max(
        depth_threshold,
        depth_threshold * 8.0 * max(abs(current_z), abs(previous_z))
    );
    if (depth_delta > relative_depth_threshold) return current;

    float3 neighborhood_min = current.rgb;
    float3 neighborhood_max = current.rgb;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            const float2 sample_uv = uv + float2(float(x), float(y)) * texel;
            const float3 sample_color = current_color.sample(point_sampler, sample_uv).rgb;
            neighborhood_min = min(neighborhood_min, sample_color);
            neighborhood_max = max(neighborhood_max, sample_color);
        }
    }

    float4 history = history_color.sample(linear_sampler, history_uv);
    history.rgb = clamp(history.rgb, neighborhood_min, neighborhood_max);
    const float motion_factor = saturate(1.0 - length(velocity) * motion_rejection);
    const float history_weight = base_history_weight * motion_factor;
    return float4(mix(current.rgb, history.rgb, history_weight), current.a);
}
)";

} // namespace vice::runtime::detail
