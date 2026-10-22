#pragma once

#include <vice/render/rhi.hpp>

#include <array>
#include <string_view>

namespace vice::runtime::detail {

inline constexpr std::array cluster_visibility_scatter_resources{
    render::ShaderResourceBindingDesc{
        .register_class = render::ShaderRegisterClass::constant_buffer,
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

inline constexpr std::array cluster_visibility_cull_resources{
    render::ShaderResourceBindingDesc{
        .register_class = render::ShaderRegisterClass::constant_buffer,
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
    render::ShaderResourceBindingDesc{
        .register_class = render::ShaderRegisterClass::unordered_access,
        .binding = 4U,
        .space = 0U,
        .count = 1U,
        .unbounded = false,
    },
    render::ShaderResourceBindingDesc{
        .register_class = render::ShaderRegisterClass::unordered_access,
        .binding = 5U,
        .space = 0U,
        .count = 1U,
        .unbounded = false,
    },
    render::ShaderResourceBindingDesc{
        .register_class = render::ShaderRegisterClass::unordered_access,
        .binding = 6U,
        .space = 0U,
        .count = 1U,
        .unbounded = false,
    },
};

inline constexpr render::ShaderBindingKey cluster_visibility_constants_binding{
    render::ShaderRegisterClass::constant_buffer, 0U, 0U, 0U,
};
inline constexpr render::ShaderBindingKey cluster_visibility_parent_count_binding{
    render::ShaderRegisterClass::unordered_access, 1U, 0U, 0U,
};
inline constexpr render::ShaderBindingKey cluster_visibility_parent_indices_binding{
    render::ShaderRegisterClass::unordered_access, 2U, 0U, 0U,
};
inline constexpr render::ShaderBindingKey cluster_visibility_parent_flags_write_binding{
    render::ShaderRegisterClass::unordered_access, 3U, 0U, 0U,
};
inline constexpr render::ShaderBindingKey cluster_visibility_candidates_binding{
    render::ShaderRegisterClass::unordered_access, 1U, 0U, 0U,
};
inline constexpr render::ShaderBindingKey cluster_visibility_meshlets_binding{
    render::ShaderRegisterClass::unordered_access, 2U, 0U, 0U,
};
inline constexpr render::ShaderBindingKey cluster_visibility_models_binding{
    render::ShaderRegisterClass::unordered_access, 3U, 0U, 0U,
};
inline constexpr render::ShaderBindingKey cluster_visibility_parent_flags_read_binding{
    render::ShaderRegisterClass::unordered_access, 4U, 0U, 0U,
};
inline constexpr render::ShaderBindingKey cluster_visibility_output_count_binding{
    render::ShaderRegisterClass::unordered_access, 5U, 0U, 0U,
};
inline constexpr render::ShaderBindingKey cluster_visibility_output_indices_binding{
    render::ShaderRegisterClass::unordered_access, 6U, 0U, 0U,
};

inline constexpr std::string_view cluster_visibility_hlsl_source = R"(
cbuffer ClusterVisibilityFrame : register(b0) {
    float4 frustum_planes[6];
    uint candidate_count;
    uint instance_count;
    uint meshlet_count;
    uint cluster_visibility_padding;
};

struct ClusterCandidate {
    uint scene_instance_index;
    uint mesh_index;
    uint meshlet_index;
    uint reserved;
};

struct MeshletRecord {
    float4 bounds_center_radius;
    uint vertex_offset;
    uint vertex_count;
    uint triangle_offset;
    uint triangle_count;
    uint source_first_index;
    uint source_index_count;
    uint mesh_index;
    uint reserved;
};

RWStructuredBuffer<uint> parent_visible_count : register(u1);
RWStructuredBuffer<uint> parent_visible_indices : register(u2);
RWStructuredBuffer<uint> parent_visible_flags_write : register(u3);

[numthreads(64, 1, 1)]
void vice_runtime_cluster_parent_scatter_cs(uint3 dispatch_id : SV_DispatchThreadID) {
    const uint compact_index = dispatch_id.x;
    const uint visible_count = min(parent_visible_count[0], instance_count);
    if (compact_index >= visible_count) return;
    const uint scene_instance = parent_visible_indices[compact_index];
    if (scene_instance < instance_count) parent_visible_flags_write[scene_instance] = 1U;
}

RWStructuredBuffer<ClusterCandidate> cluster_candidates : register(u1);
RWStructuredBuffer<MeshletRecord> meshlet_records : register(u2);
RWStructuredBuffer<float4x4> camera_relative_models : register(u3);
RWStructuredBuffer<uint> parent_visible_flags_read : register(u4);
RWStructuredBuffer<uint> cluster_visible_count : register(u5);
RWStructuredBuffer<uint> cluster_visible_indices : register(u6);

[numthreads(64, 1, 1)]
void vice_runtime_cluster_visibility_cs(uint3 dispatch_id : SV_DispatchThreadID) {
    const uint candidate_index = dispatch_id.x;
    if (candidate_index >= candidate_count) return;

    const ClusterCandidate candidate = cluster_candidates[candidate_index];
    if (candidate.scene_instance_index >= instance_count ||
        candidate.meshlet_index >= meshlet_count ||
        parent_visible_flags_read[candidate.scene_instance_index] == 0U) {
        return;
    }

    const MeshletRecord meshlet = meshlet_records[candidate.meshlet_index];
    if (meshlet.mesh_index != candidate.mesh_index) return;
    const float4x4 model = camera_relative_models[candidate.scene_instance_index];
    const float3 center = mul(
        model,
        float4(meshlet.bounds_center_radius.xyz, 1.0)
    ).xyz;
    const float scale_x = length(mul(model, float4(1.0, 0.0, 0.0, 0.0)).xyz);
    const float scale_y = length(mul(model, float4(0.0, 1.0, 0.0, 0.0)).xyz);
    const float scale_z = length(mul(model, float4(0.0, 0.0, 1.0, 0.0)).xyz);
    const float radius = meshlet.bounds_center_radius.w * max(scale_x, max(scale_y, scale_z));

    bool visible = !any(isnan(center)) && !any(isinf(center)) &&
        !isnan(radius) && !isinf(radius) && radius >= 0.0;
    [unroll]
    for (uint plane_index = 0U; plane_index < 6U; ++plane_index) {
        const float distance = dot(frustum_planes[plane_index].xyz, center) +
            frustum_planes[plane_index].w;
        visible = visible && distance >= -radius;
    }
    if (!visible) return;

    uint output_index = 0U;
    InterlockedAdd(cluster_visible_count[0], 1U, output_index);
    cluster_visible_indices[output_index] = candidate_index;
}
)";

inline constexpr std::string_view cluster_visibility_metal_source = R"(
#include <metal_stdlib>
using namespace metal;

struct ClusterVisibilityFrame {
    float4 frustum_planes[6];
    uint candidate_count;
    uint instance_count;
    uint meshlet_count;
    uint padding;
};

struct ClusterCandidate {
    uint scene_instance_index;
    uint mesh_index;
    uint meshlet_index;
    uint reserved;
};

struct MeshletRecord {
    float4 bounds_center_radius;
    uint vertex_offset;
    uint vertex_count;
    uint triangle_offset;
    uint triangle_count;
    uint source_first_index;
    uint source_index_count;
    uint mesh_index;
    uint reserved;
};

kernel void vice_runtime_cluster_parent_scatter_cs(
    constant ClusterVisibilityFrame& frame [[buffer(0)]],
    device uint* parent_visible_count [[buffer(1)]],
    device uint* parent_visible_indices [[buffer(2)]],
    device uint* parent_visible_flags [[buffer(3)]],
    uint3 dispatch_id [[thread_position_in_grid]]) {
    const uint compact_index = dispatch_id.x;
    const uint visible_count = min(parent_visible_count[0], frame.instance_count);
    if (compact_index >= visible_count) return;
    const uint scene_instance = parent_visible_indices[compact_index];
    if (scene_instance < frame.instance_count) parent_visible_flags[scene_instance] = 1U;
}

kernel void vice_runtime_cluster_visibility_cs(
    constant ClusterVisibilityFrame& frame [[buffer(0)]],
    device ClusterCandidate* cluster_candidates [[buffer(1)]],
    device MeshletRecord* meshlet_records [[buffer(2)]],
    device float4x4* camera_relative_models [[buffer(3)]],
    device uint* parent_visible_flags [[buffer(4)]],
    device atomic_uint* cluster_visible_count [[buffer(5)]],
    device uint* cluster_visible_indices [[buffer(6)]],
    uint3 dispatch_id [[thread_position_in_grid]]) {
    const uint candidate_index = dispatch_id.x;
    if (candidate_index >= frame.candidate_count) return;

    const ClusterCandidate candidate = cluster_candidates[candidate_index];
    if (candidate.scene_instance_index >= frame.instance_count ||
        candidate.meshlet_index >= frame.meshlet_count ||
        parent_visible_flags[candidate.scene_instance_index] == 0U) {
        return;
    }

    const MeshletRecord meshlet = meshlet_records[candidate.meshlet_index];
    if (meshlet.mesh_index != candidate.mesh_index) return;
    const float4x4 model = camera_relative_models[candidate.scene_instance_index];
    const float3 center = (model * float4(meshlet.bounds_center_radius.xyz, 1.0)).xyz;
    const float scale_x = length((model * float4(1.0, 0.0, 0.0, 0.0)).xyz);
    const float scale_y = length((model * float4(0.0, 1.0, 0.0, 0.0)).xyz);
    const float scale_z = length((model * float4(0.0, 0.0, 1.0, 0.0)).xyz);
    const float radius = meshlet.bounds_center_radius.w * max(scale_x, max(scale_y, scale_z));

    bool visible = all(isfinite(center)) && isfinite(radius) && radius >= 0.0f;
    for (uint plane_index = 0U; plane_index < 6U; ++plane_index) {
        const float distance = dot(frame.frustum_planes[plane_index].xyz, center) +
            frame.frustum_planes[plane_index].w;
        visible = visible && distance >= -radius;
    }
    if (!visible) return;

    const uint output_index = atomic_fetch_add_explicit(
        &cluster_visible_count[0],
        1U,
        memory_order_relaxed
    );
    cluster_visible_indices[output_index] = candidate_index;
}
)";

} // namespace vice::runtime::detail
