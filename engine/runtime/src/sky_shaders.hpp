#pragma once

#include <vice/render/rhi.hpp>

#include <array>
#include <string_view>

namespace vice::runtime::detail {

struct SkyConstants {
    float camera_forward_tan_half_fov[4]{};
    float camera_right_aspect[4]{};
    float camera_up_rayleigh[4]{};
    float sun_direction_mie[4]{};
    float sun_color_intensity[4]{};
    float atmosphere_parameters[4]{};
    float ground_albedo[4]{};
};

static_assert(sizeof(SkyConstants) == 112U);

inline constexpr render::ShaderBindingKey sky_constants_binding{
    .register_class = render::ShaderRegisterClass::constant_buffer,
    .binding = 0,
    .space = 0,
    .array_element = 0,
};

inline constexpr std::array sky_fragment_resources{
    render::ShaderResourceBindingDesc{
        .register_class = render::ShaderRegisterClass::constant_buffer,
        .binding = 0,
        .space = 0,
        .count = 1,
        .unbounded = false,
    },
};

inline constexpr std::string_view sky_hlsl_source = R"(
static const float VICE_PI = 3.14159265358979323846;

cbuffer SkyFrame : register(b0, space0) {
    float4 camera_forward_tan_half_fov;
    float4 camera_right_aspect;
    float4 camera_up_rayleigh;
    float4 sun_direction_mie;
    float4 sun_color_intensity;
    float4 atmosphere_parameters;
    float4 ground_albedo;
};

struct SkyVertexOut {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

SkyVertexOut vice_runtime_sky_vs(uint vertex_id : SV_VertexID) {
    static const float2 uvs[3] = {
        float2(0.0, 0.0),
        float2(2.0, 0.0),
        float2(0.0, 2.0)
    };
    SkyVertexOut output;
    output.uv = uvs[vertex_id];
    output.position = float4(
        output.uv.x * 2.0 - 1.0,
        1.0 - output.uv.y * 2.0,
        0.0,
        1.0
    );
    return output;
}

float3 vice_sky_radiance(float3 view_direction) {
    view_direction = normalize(view_direction);
    const float3 sun_direction = normalize(sun_direction_mie.xyz);
    const float3 sun_color = max(sun_color_intensity.rgb, 0.0);
    const float sun_intensity = max(sun_color_intensity.w, 0.0);
    const float rayleigh_strength = max(camera_up_rayleigh.w, 0.0);
    const float mie_strength = max(sun_direction_mie.w, 0.0);
    const float g = clamp(atmosphere_parameters.x, 0.0, 0.98);
    const float sun_radius = max(atmosphere_parameters.y, 0.0001);
    const float horizon_falloff = max(atmosphere_parameters.w, 0.01);

    const float mu = clamp(dot(view_direction, sun_direction), -1.0, 1.0);
    const float rayleigh_phase = (3.0 / (16.0 * VICE_PI)) * (1.0 + mu * mu);
    const float mie_denominator = max(1.0 + g * g - 2.0 * g * mu, 0.0001);
    const float mie_phase = ((1.0 - g * g) / (4.0 * VICE_PI)) /
        pow(mie_denominator, 1.5);

    const float altitude = max(view_direction.y, 0.0);
    const float air_mass = 1.0 / max(0.10 + altitude * 0.90, 0.10);
    const float3 beta_rayleigh = float3(0.0058, 0.0135, 0.0331) * rayleigh_strength;
    const float3 beta_mie = float3(0.021, 0.021, 0.021) * mie_strength;
    const float3 extinction = exp(-(beta_rayleigh + beta_mie) * air_mass * 1.35);
    const float3 phase_scatter =
        beta_rayleigh * rayleigh_phase + beta_mie * mie_phase;
    float3 sky = sun_color * sun_intensity * phase_scatter * air_mass * 24.0;
    sky *= 0.35 + 0.65 * (1.0 - extinction);

    const float horizon = exp(-altitude / horizon_falloff);
    sky += sun_color * sun_intensity * horizon * float3(0.018, 0.011, 0.006);

    const float disk_inner = cos(sun_radius * 0.55);
    const float disk_outer = cos(sun_radius * 2.2);
    const float sun_disk = smoothstep(disk_outer, disk_inner, mu);
    sky += sun_color * sun_intensity * sun_disk * 7.5;

    const float sun_height = saturate(sun_direction.y * 0.5 + 0.5);
    const float3 ground = max(ground_albedo.rgb, 0.0) *
        sun_color * (0.12 + sun_intensity * 0.07 * sun_height);
    const float sky_weight = smoothstep(-0.08, 0.025, view_direction.y);
    return max(lerp(ground, sky, sky_weight), 0.0);
}

float4 vice_runtime_sky_fs(SkyVertexOut input) : SV_Target0 {
    const float2 centered = float2(input.uv.x * 2.0 - 1.0, 1.0 - input.uv.y * 2.0);
    const float tan_half_fov = camera_forward_tan_half_fov.w;
    const float aspect = camera_right_aspect.w;
    const float3 view_direction = normalize(
        camera_forward_tan_half_fov.xyz +
        camera_right_aspect.xyz * (centered.x * aspect * tan_half_fov) +
        camera_up_rayleigh.xyz * (centered.y * tan_half_fov)
    );
    return float4(vice_sky_radiance(view_direction), 1.0);
}
)";

inline constexpr std::string_view sky_metal_source = R"(
#include <metal_stdlib>
using namespace metal;

constant float VICE_PI = 3.14159265358979323846;

struct SkyFrame {
    float4 camera_forward_tan_half_fov;
    float4 camera_right_aspect;
    float4 camera_up_rayleigh;
    float4 sun_direction_mie;
    float4 sun_color_intensity;
    float4 atmosphere_parameters;
    float4 ground_albedo;
};

struct SkyVertexOut {
    float4 position [[position]];
    float2 uv;
};

vertex SkyVertexOut vice_runtime_sky_vs(uint vertex_id [[vertex_id]]) {
    constexpr float2 uvs[3] = {
        float2(0.0, 0.0),
        float2(2.0, 0.0),
        float2(0.0, 2.0)
    };
    SkyVertexOut output;
    output.uv = uvs[vertex_id];
    output.position = float4(
        output.uv.x * 2.0 - 1.0,
        1.0 - output.uv.y * 2.0,
        0.0,
        1.0
    );
    return output;
}

float3 vice_sky_radiance(float3 view_direction, constant SkyFrame& frame) {
    view_direction = normalize(view_direction);
    const float3 sun_direction = normalize(frame.sun_direction_mie.xyz);
    const float3 sun_color = max(frame.sun_color_intensity.rgb, float3(0.0));
    const float sun_intensity = max(frame.sun_color_intensity.w, 0.0f);
    const float rayleigh_strength = max(frame.camera_up_rayleigh.w, 0.0f);
    const float mie_strength = max(frame.sun_direction_mie.w, 0.0f);
    const float g = clamp(frame.atmosphere_parameters.x, 0.0f, 0.98f);
    const float sun_radius = max(frame.atmosphere_parameters.y, 0.0001f);
    const float horizon_falloff = max(frame.atmosphere_parameters.w, 0.01f);

    const float mu = clamp(dot(view_direction, sun_direction), -1.0f, 1.0f);
    const float rayleigh_phase = (3.0f / (16.0f * VICE_PI)) * (1.0f + mu * mu);
    const float mie_denominator = max(1.0f + g * g - 2.0f * g * mu, 0.0001f);
    const float mie_phase = ((1.0f - g * g) / (4.0f * VICE_PI)) /
        pow(mie_denominator, 1.5f);

    const float altitude = max(view_direction.y, 0.0f);
    const float air_mass = 1.0f / max(0.10f + altitude * 0.90f, 0.10f);
    const float3 beta_rayleigh = float3(0.0058f, 0.0135f, 0.0331f) * rayleigh_strength;
    const float3 beta_mie = float3(0.021f) * mie_strength;
    const float3 extinction = exp(-(beta_rayleigh + beta_mie) * air_mass * 1.35f);
    const float3 phase_scatter = beta_rayleigh * rayleigh_phase + beta_mie * mie_phase;
    float3 sky = sun_color * sun_intensity * phase_scatter * air_mass * 24.0f;
    sky *= 0.35f + 0.65f * (1.0f - extinction);

    const float horizon = exp(-altitude / horizon_falloff);
    sky += sun_color * sun_intensity * horizon * float3(0.018f, 0.011f, 0.006f);

    const float disk_inner = cos(sun_radius * 0.55f);
    const float disk_outer = cos(sun_radius * 2.2f);
    const float sun_disk = smoothstep(disk_outer, disk_inner, mu);
    sky += sun_color * sun_intensity * sun_disk * 7.5f;

    const float sun_height = clamp(sun_direction.y * 0.5f + 0.5f, 0.0f, 1.0f);
    const float3 ground = max(frame.ground_albedo.rgb, float3(0.0f)) *
        sun_color * (0.12f + sun_intensity * 0.07f * sun_height);
    const float sky_weight = smoothstep(-0.08f, 0.025f, view_direction.y);
    return max(mix(ground, sky, sky_weight), float3(0.0f));
}

fragment float4 vice_runtime_sky_fs(
    SkyVertexOut input [[stage_in]],
    constant SkyFrame& frame [[buffer(0)]]) {
    const float2 centered = float2(input.uv.x * 2.0f - 1.0f, 1.0f - input.uv.y * 2.0f);
    const float tan_half_fov = frame.camera_forward_tan_half_fov.w;
    const float aspect = frame.camera_right_aspect.w;
    const float3 view_direction = normalize(
        frame.camera_forward_tan_half_fov.xyz +
        frame.camera_right_aspect.xyz * (centered.x * aspect * tan_half_fov) +
        frame.camera_up_rayleigh.xyz * (centered.y * tan_half_fov)
    );
    return float4(vice_sky_radiance(view_direction, frame), 1.0f);
}
)";

} // namespace vice::runtime::detail
