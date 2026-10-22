#pragma once

#include <vice/render/rhi.hpp>
#include <vice/runtime/hiz_occlusion.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace vice::runtime::detail {

inline constexpr auto hiz_occlusion_resources = [] {
    std::array<render::ShaderResourceBindingDesc, 23U> result{};
    std::size_t index = 0U;
    result[index++] = {
        render::ShaderRegisterClass::constant_buffer, 0U, 0U, 1U, false,
    };
    for (std::uint32_t binding = 1U; binding <= 5U; ++binding) {
        result[index++] = {
            render::ShaderRegisterClass::unordered_access, binding, 0U, 1U, false,
        };
    }
    for (std::uint32_t binding = 0U; binding < runtime_hiz_occlusion_max_levels; ++binding) {
        result[index++] = {
            render::ShaderRegisterClass::shader_resource, binding, 0U, 1U, false,
        };
    }
    result[index] = {
        render::ShaderRegisterClass::sampler, 0U, 0U, 1U, false,
    };
    return result;
}();

inline constexpr render::ShaderBindingKey hiz_occlusion_constants_binding{
    render::ShaderRegisterClass::constant_buffer, 0U, 0U, 0U,
};
inline constexpr render::ShaderBindingKey hiz_occlusion_input_count_binding{
    render::ShaderRegisterClass::unordered_access, 1U, 0U, 0U,
};
inline constexpr render::ShaderBindingKey hiz_occlusion_input_indices_binding{
    render::ShaderRegisterClass::unordered_access, 2U, 0U, 0U,
};
inline constexpr render::ShaderBindingKey hiz_occlusion_spheres_binding{
    render::ShaderRegisterClass::unordered_access, 3U, 0U, 0U,
};
inline constexpr render::ShaderBindingKey hiz_occlusion_output_count_binding{
    render::ShaderRegisterClass::unordered_access, 4U, 0U, 0U,
};
inline constexpr render::ShaderBindingKey hiz_occlusion_output_indices_binding{
    render::ShaderRegisterClass::unordered_access, 5U, 0U, 0U,
};
inline constexpr render::ShaderBindingKey hiz_occlusion_sampler_binding{
    render::ShaderRegisterClass::sampler, 0U, 0U, 0U,
};

[[nodiscard]] inline constexpr render::ShaderBindingKey hiz_occlusion_level_binding(
    const std::uint32_t level
) noexcept {
    return {
        render::ShaderRegisterClass::shader_resource, level, 0U, 0U,
    };
}

inline constexpr std::string_view hiz_occlusion_hlsl_source = R"(
cbuffer HiZOcclusionFrame : register(b0) {
    float4x4 view_projection;
    float4 projection_data;
    float4 occlusion_data;
    uint4 visibility_data;
    uint4 viewport_data;
};

RWStructuredBuffer<uint> frustum_visible_count : register(u1);
RWStructuredBuffer<uint> frustum_visible_indices : register(u2);
RWStructuredBuffer<float4> instance_spheres : register(u3);
RWStructuredBuffer<uint> occlusion_visible_count : register(u4);
RWStructuredBuffer<uint> occlusion_visible_indices : register(u5);

Texture2D<float> hiz_0 : register(t0);
Texture2D<float> hiz_1 : register(t1);
Texture2D<float> hiz_2 : register(t2);
Texture2D<float> hiz_3 : register(t3);
Texture2D<float> hiz_4 : register(t4);
Texture2D<float> hiz_5 : register(t5);
Texture2D<float> hiz_6 : register(t6);
Texture2D<float> hiz_7 : register(t7);
Texture2D<float> hiz_8 : register(t8);
Texture2D<float> hiz_9 : register(t9);
Texture2D<float> hiz_10 : register(t10);
Texture2D<float> hiz_11 : register(t11);
Texture2D<float> hiz_12 : register(t12);
Texture2D<float> hiz_13 : register(t13);
Texture2D<float> hiz_14 : register(t14);
Texture2D<float> hiz_15 : register(t15);
SamplerState hiz_sampler : register(s0);

float vice_hiz_sample_level(uint level, float2 uv) {
    switch (level) {
    case 0U: return hiz_0.SampleLevel(hiz_sampler, uv, 0.0);
    case 1U: return hiz_1.SampleLevel(hiz_sampler, uv, 0.0);
    case 2U: return hiz_2.SampleLevel(hiz_sampler, uv, 0.0);
    case 3U: return hiz_3.SampleLevel(hiz_sampler, uv, 0.0);
    case 4U: return hiz_4.SampleLevel(hiz_sampler, uv, 0.0);
    case 5U: return hiz_5.SampleLevel(hiz_sampler, uv, 0.0);
    case 6U: return hiz_6.SampleLevel(hiz_sampler, uv, 0.0);
    case 7U: return hiz_7.SampleLevel(hiz_sampler, uv, 0.0);
    case 8U: return hiz_8.SampleLevel(hiz_sampler, uv, 0.0);
    case 9U: return hiz_9.SampleLevel(hiz_sampler, uv, 0.0);
    case 10U: return hiz_10.SampleLevel(hiz_sampler, uv, 0.0);
    case 11U: return hiz_11.SampleLevel(hiz_sampler, uv, 0.0);
    case 12U: return hiz_12.SampleLevel(hiz_sampler, uv, 0.0);
    case 13U: return hiz_13.SampleLevel(hiz_sampler, uv, 0.0);
    case 14U: return hiz_14.SampleLevel(hiz_sampler, uv, 0.0);
    default: return hiz_15.SampleLevel(hiz_sampler, uv, 0.0);
    }
}

bool vice_hiz_occluded(float4 sphere) {
    const float radius = max(sphere.w, 0.0);
    const float near_plane = projection_data.z;
    const float far_plane = projection_data.w;
    const float4 clip = mul(view_projection, float4(sphere.xyz, 1.0));
    if (!all(isfinite(clip)) || clip.w <= radius + near_plane) return false;

    const float near_w = clip.w - radius;
    if (!isfinite(near_w) || near_w <= near_plane) return false;

    const float2 ndc_center = clip.xy / clip.w;
    const float2 ndc_radius = projection_data.xy * radius / near_w;
    if (!all(isfinite(ndc_center)) || !all(isfinite(ndc_radius))) return false;

    float2 ndc_min = ndc_center - ndc_radius;
    float2 ndc_max = ndc_center + ndc_radius;
    if (ndc_max.x < -1.0 || ndc_min.x > 1.0 ||
        ndc_max.y < -1.0 || ndc_min.y > 1.0) return false;
    ndc_min = max(ndc_min, float2(-1.0, -1.0));
    ndc_max = min(ndc_max, float2(1.0, 1.0));

    const float2 uv_min = float2(
        ndc_min.x * 0.5 + 0.5,
        0.5 - ndc_max.y * 0.5
    );
    const float2 uv_max = float2(
        ndc_max.x * 0.5 + 0.5,
        0.5 - ndc_min.y * 0.5
    );
    const float2 extent_pixels = max(
        (uv_max - uv_min) * float2(viewport_data.xy),
        float2(1.0, 1.0)
    );
    const float max_extent = max(extent_pixels.x, extent_pixels.y);
    const uint level_count = max(visibility_data.z, 1U);
    const uint requested_level = (uint)floor(log2(max(max_extent, 1.0)));
    const uint level = min(requested_level, level_count - 1U);

    const float depth_00 = vice_hiz_sample_level(level, uv_min);
    const float depth_10 = vice_hiz_sample_level(level, float2(uv_max.x, uv_min.y));
    const float depth_01 = vice_hiz_sample_level(level, float2(uv_min.x, uv_max.y));
    const float depth_11 = vice_hiz_sample_level(level, uv_max);
    const float max_depth = max(max(depth_00, depth_10), max(depth_01, depth_11));
    if (!isfinite(max_depth)) return false;

    const float depth_denominator = far_plane - near_plane;
    if (!(depth_denominator > 0.0)) return false;
    const float nearest_depth = far_plane / depth_denominator -
        (far_plane * near_plane) / (depth_denominator * near_w);
    if (!isfinite(nearest_depth)) return false;
    return saturate(nearest_depth) > max_depth + max(occlusion_data.x, 0.0);
}

[numthreads(64, 1, 1)]
void vice_runtime_hiz_occlusion_cs(uint3 dispatch_id : SV_DispatchThreadID) {
    const uint visible_slot = dispatch_id.x;
    const uint visible_capacity = visibility_data.x;
    if (visible_slot >= visible_capacity || visible_slot >= frustum_visible_count[0]) return;

    const uint instance_index = frustum_visible_indices[visible_slot];
    if (instance_index >= visibility_data.y) return;
    if (vice_hiz_occluded(instance_spheres[instance_index])) return;

    uint output_slot = 0U;
    InterlockedAdd(occlusion_visible_count[0], 1U, output_slot);
    if (output_slot < visible_capacity) {
        occlusion_visible_indices[output_slot] = instance_index;
    }
}
)";

inline constexpr std::string_view hiz_occlusion_metal_source = R"(
#include <metal_stdlib>
using namespace metal;

struct HiZOcclusionFrame {
    float4x4 view_projection;
    float4 projection_data;
    float4 occlusion_data;
    uint4 visibility_data;
    uint4 viewport_data;
};

float vice_hiz_sample_level(
    uint level,
    float2 uv,
    texture2d<float> hiz_0,
    texture2d<float> hiz_1,
    texture2d<float> hiz_2,
    texture2d<float> hiz_3,
    texture2d<float> hiz_4,
    texture2d<float> hiz_5,
    texture2d<float> hiz_6,
    texture2d<float> hiz_7,
    texture2d<float> hiz_8,
    texture2d<float> hiz_9,
    texture2d<float> hiz_10,
    texture2d<float> hiz_11,
    texture2d<float> hiz_12,
    texture2d<float> hiz_13,
    texture2d<float> hiz_14,
    texture2d<float> hiz_15,
    sampler hiz_sampler) {
    switch (level) {
    case 0U: return hiz_0.sample(hiz_sampler, uv).r;
    case 1U: return hiz_1.sample(hiz_sampler, uv).r;
    case 2U: return hiz_2.sample(hiz_sampler, uv).r;
    case 3U: return hiz_3.sample(hiz_sampler, uv).r;
    case 4U: return hiz_4.sample(hiz_sampler, uv).r;
    case 5U: return hiz_5.sample(hiz_sampler, uv).r;
    case 6U: return hiz_6.sample(hiz_sampler, uv).r;
    case 7U: return hiz_7.sample(hiz_sampler, uv).r;
    case 8U: return hiz_8.sample(hiz_sampler, uv).r;
    case 9U: return hiz_9.sample(hiz_sampler, uv).r;
    case 10U: return hiz_10.sample(hiz_sampler, uv).r;
    case 11U: return hiz_11.sample(hiz_sampler, uv).r;
    case 12U: return hiz_12.sample(hiz_sampler, uv).r;
    case 13U: return hiz_13.sample(hiz_sampler, uv).r;
    case 14U: return hiz_14.sample(hiz_sampler, uv).r;
    default: return hiz_15.sample(hiz_sampler, uv).r;
    }
}

kernel void vice_runtime_hiz_occlusion_cs(
    constant HiZOcclusionFrame& frame [[buffer(0)]],
    device uint* frustum_visible_count [[buffer(1)]],
    device uint* frustum_visible_indices [[buffer(2)]],
    device float4* instance_spheres [[buffer(3)]],
    device atomic_uint* occlusion_visible_count [[buffer(4)]],
    device uint* occlusion_visible_indices [[buffer(5)]],
    texture2d<float> hiz_0 [[texture(0)]],
    texture2d<float> hiz_1 [[texture(1)]],
    texture2d<float> hiz_2 [[texture(2)]],
    texture2d<float> hiz_3 [[texture(3)]],
    texture2d<float> hiz_4 [[texture(4)]],
    texture2d<float> hiz_5 [[texture(5)]],
    texture2d<float> hiz_6 [[texture(6)]],
    texture2d<float> hiz_7 [[texture(7)]],
    texture2d<float> hiz_8 [[texture(8)]],
    texture2d<float> hiz_9 [[texture(9)]],
    texture2d<float> hiz_10 [[texture(10)]],
    texture2d<float> hiz_11 [[texture(11)]],
    texture2d<float> hiz_12 [[texture(12)]],
    texture2d<float> hiz_13 [[texture(13)]],
    texture2d<float> hiz_14 [[texture(14)]],
    texture2d<float> hiz_15 [[texture(15)]],
    sampler hiz_sampler [[sampler(0)]],
    uint3 dispatch_id [[thread_position_in_grid]]) {
    const uint visible_slot = dispatch_id.x;
    const uint visible_capacity = frame.visibility_data.x;
    if (visible_slot >= visible_capacity || visible_slot >= frustum_visible_count[0]) return;

    const uint instance_index = frustum_visible_indices[visible_slot];
    if (instance_index >= frame.visibility_data.y) return;

    const float4 sphere = instance_spheres[instance_index];
    const float radius = max(sphere.w, 0.0f);
    const float near_plane = frame.projection_data.z;
    const float far_plane = frame.projection_data.w;
    const float4 clip = frame.view_projection * float4(sphere.xyz, 1.0f);

    bool occluded = false;
    if (all(isfinite(clip)) && clip.w > radius + near_plane) {
        const float near_w = clip.w - radius;
        if (isfinite(near_w) && near_w > near_plane) {
            const float2 ndc_center = clip.xy / clip.w;
            const float2 ndc_radius = frame.projection_data.xy * radius / near_w;
            if (all(isfinite(ndc_center)) && all(isfinite(ndc_radius))) {
                float2 ndc_min = ndc_center - ndc_radius;
                float2 ndc_max = ndc_center + ndc_radius;
                const bool overlaps_screen = !(
                    ndc_max.x < -1.0f || ndc_min.x > 1.0f ||
                    ndc_max.y < -1.0f || ndc_min.y > 1.0f
                );
                if (overlaps_screen) {
                    ndc_min = max(ndc_min, float2(-1.0f));
                    ndc_max = min(ndc_max, float2(1.0f));
                    const float2 uv_min = float2(
                        ndc_min.x * 0.5f + 0.5f,
                        0.5f - ndc_max.y * 0.5f
                    );
                    const float2 uv_max = float2(
                        ndc_max.x * 0.5f + 0.5f,
                        0.5f - ndc_min.y * 0.5f
                    );
                    const float2 extent_pixels = max(
                        (uv_max - uv_min) * float2(frame.viewport_data.xy),
                        float2(1.0f)
                    );
                    const float max_extent = max(extent_pixels.x, extent_pixels.y);
                    const uint level_count = max(frame.visibility_data.z, 1U);
                    const uint requested_level = uint(floor(log2(max(max_extent, 1.0f))));
                    const uint level = min(requested_level, level_count - 1U);

                    const float depth_00 = vice_hiz_sample_level(
                        level, uv_min, hiz_0, hiz_1, hiz_2, hiz_3, hiz_4, hiz_5, hiz_6,
                        hiz_7, hiz_8, hiz_9, hiz_10, hiz_11, hiz_12, hiz_13, hiz_14,
                        hiz_15, hiz_sampler
                    );
                    const float depth_10 = vice_hiz_sample_level(
                        level, float2(uv_max.x, uv_min.y), hiz_0, hiz_1, hiz_2, hiz_3,
                        hiz_4, hiz_5, hiz_6, hiz_7, hiz_8, hiz_9, hiz_10, hiz_11,
                        hiz_12, hiz_13, hiz_14, hiz_15, hiz_sampler
                    );
                    const float depth_01 = vice_hiz_sample_level(
                        level, float2(uv_min.x, uv_max.y), hiz_0, hiz_1, hiz_2, hiz_3,
                        hiz_4, hiz_5, hiz_6, hiz_7, hiz_8, hiz_9, hiz_10, hiz_11,
                        hiz_12, hiz_13, hiz_14, hiz_15, hiz_sampler
                    );
                    const float depth_11 = vice_hiz_sample_level(
                        level, uv_max, hiz_0, hiz_1, hiz_2, hiz_3, hiz_4, hiz_5, hiz_6,
                        hiz_7, hiz_8, hiz_9, hiz_10, hiz_11, hiz_12, hiz_13, hiz_14,
                        hiz_15, hiz_sampler
                    );
                    const float max_depth = max(max(depth_00, depth_10), max(depth_01, depth_11));
                    const float depth_denominator = far_plane - near_plane;
                    if (isfinite(max_depth) && depth_denominator > 0.0f) {
                        const float nearest_depth = far_plane / depth_denominator -
                            (far_plane * near_plane) / (depth_denominator * near_w);
                        if (isfinite(nearest_depth)) {
                            occluded = clamp(nearest_depth, 0.0f, 1.0f) >
                                max_depth + max(frame.occlusion_data.x, 0.0f);
                        }
                    }
                }
            }
        }
    }

    if (occluded) return;
    const uint output_slot = atomic_fetch_add_explicit(
        &occlusion_visible_count[0], 1U, memory_order_relaxed
    );
    if (output_slot < visible_capacity) {
        occlusion_visible_indices[output_slot] = instance_index;
    }
}
)";

} // namespace vice::runtime::detail
