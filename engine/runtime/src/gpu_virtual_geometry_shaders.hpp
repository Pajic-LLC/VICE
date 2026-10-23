#pragma once

#include <vice/render/rhi.hpp>

#include <array>
#include <string_view>

namespace vice::runtime::detail {

inline constexpr std::array gpu_virtual_geometry_resources{
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
    render::ShaderResourceBindingDesc{
        .register_class = render::ShaderRegisterClass::unordered_access,
        .binding = 7U,
        .space = 0U,
        .count = 1U,
        .unbounded = false,
    },
};

inline constexpr render::ShaderBindingKey gpu_virtual_geometry_constants_binding{
    render::ShaderRegisterClass::constant_buffer, 0U, 0U, 0U,
};
inline constexpr render::ShaderBindingKey gpu_virtual_geometry_nodes_binding{
    render::ShaderRegisterClass::unordered_access, 1U, 0U, 0U,
};
inline constexpr render::ShaderBindingKey gpu_virtual_geometry_candidates_binding{
    render::ShaderRegisterClass::unordered_access, 2U, 0U, 0U,
};
inline constexpr render::ShaderBindingKey gpu_virtual_geometry_models_binding{
    render::ShaderRegisterClass::unordered_access, 3U, 0U, 0U,
};
inline constexpr render::ShaderBindingKey gpu_virtual_geometry_node_count_binding{
    render::ShaderRegisterClass::unordered_access, 4U, 0U, 0U,
};
inline constexpr render::ShaderBindingKey gpu_virtual_geometry_node_requests_binding{
    render::ShaderRegisterClass::unordered_access, 5U, 0U, 0U,
};
inline constexpr render::ShaderBindingKey gpu_virtual_geometry_page_count_binding{
    render::ShaderRegisterClass::unordered_access, 6U, 0U, 0U,
};
inline constexpr render::ShaderBindingKey gpu_virtual_geometry_page_requests_binding{
    render::ShaderRegisterClass::unordered_access, 7U, 0U, 0U,
};

inline constexpr std::string_view gpu_virtual_geometry_hlsl_source = R"(
cbuffer VirtualGeometryFrame : register(b0) {
    float4 frustum_planes[6];
    float projection_scale;
    float max_projected_error_pixels;
    uint candidate_count;
    uint node_count;
    uint instance_count;
    uint3 virtual_geometry_padding;
};

struct VirtualGeometryNode {
    float4 bounds_center_radius;
    float geometric_error;
    uint parent_index;
    uint first_child;
    uint child_count;
    uint page_offset;
    uint page_count;
    uint depth;
    uint mesh_index;
    uint local_node_index;
    uint3 reserved;
};

struct VirtualGeometryCandidate {
    uint scene_instance_index;
    uint node_index;
    uint mesh_index;
    uint local_node_index;
};

struct VirtualGeometryNodeRequest {
    uint scene_instance_index;
    uint mesh_index;
    uint node_index;
    uint reserved;
};

struct VirtualGeometryPageRequest {
    uint scene_instance_index;
    uint mesh_index;
    uint node_index;
    uint page_index;
};

RWStructuredBuffer<VirtualGeometryNode> virtual_geometry_nodes : register(u1);
RWStructuredBuffer<VirtualGeometryCandidate> virtual_geometry_candidates : register(u2);
RWStructuredBuffer<float4x4> camera_relative_models : register(u3);
RWStructuredBuffer<uint> selected_node_count : register(u4);
RWStructuredBuffer<VirtualGeometryNodeRequest> selected_node_requests : register(u5);
RWStructuredBuffer<uint> page_request_count : register(u6);
RWStructuredBuffer<VirtualGeometryPageRequest> page_requests : register(u7);

float virtual_geometry_radius_scale(float4x4 model) {
    const float3 column0 = mul(model, float4(1.0, 0.0, 0.0, 0.0)).xyz;
    const float3 column1 = mul(model, float4(0.0, 1.0, 0.0, 0.0)).xyz;
    const float3 column2 = mul(model, float4(0.0, 0.0, 1.0, 0.0)).xyz;
    const float3 one = float3(1.0, 1.0, 1.0);
    const float one_norm = max(dot(abs(column0), one), max(dot(abs(column1), one), dot(abs(column2), one)));
    const float infinity_norm = max(
        abs(column0.x) + abs(column1.x) + abs(column2.x),
        max(
            abs(column0.y) + abs(column1.y) + abs(column2.y),
            abs(column0.z) + abs(column1.z) + abs(column2.z)
        )
    );
    return sqrt(max(0.0, one_norm * infinity_norm));
}

bool virtual_geometry_visible(
    VirtualGeometryNode node,
    float4x4 model,
    float radius_scale,
    out float3 center,
    out float radius
) {
    center = mul(model, float4(node.bounds_center_radius.xyz, 1.0)).xyz;
    radius = node.bounds_center_radius.w * radius_scale;
    bool visible = !any(isnan(center)) && !any(isinf(center)) &&
        !isnan(radius) && !isinf(radius) && radius >= 0.0;
    [unroll]
    for (uint plane_index = 0U; plane_index < 6U; ++plane_index) {
        const float distance = dot(frustum_planes[plane_index].xyz, center) +
            frustum_planes[plane_index].w;
        visible = visible && distance >= -radius;
    }
    return visible;
}

float virtual_geometry_projected_error(
    VirtualGeometryNode node,
    float3 center,
    float radius,
    float radius_scale
) {
    const float geometric_error = node.geometric_error * radius_scale;
    if (geometric_error <= 0.0) return 0.0;
    const float surface_distance = length(center) - radius;
    if (isnan(surface_distance) || isinf(surface_distance) || surface_distance <= 1.0e-6) {
        return 3.402823466e+38F;
    }
    return geometric_error * projection_scale / surface_distance;
}

[numthreads(64, 1, 1)]
void vice_runtime_virtual_geometry_select_cs(uint3 dispatch_id : SV_DispatchThreadID) {
    const uint candidate_index = dispatch_id.x;
    if (candidate_index >= candidate_count) return;

    const VirtualGeometryCandidate candidate = virtual_geometry_candidates[candidate_index];
    if (candidate.scene_instance_index >= instance_count || candidate.node_index >= node_count) return;
    const VirtualGeometryNode node = virtual_geometry_nodes[candidate.node_index];
    if (node.mesh_index != candidate.mesh_index || node.local_node_index != candidate.local_node_index) return;

    const float4x4 model = camera_relative_models[candidate.scene_instance_index];
    const float radius_scale = virtual_geometry_radius_scale(model);
    if (isnan(radius_scale) || isinf(radius_scale) || radius_scale < 0.0) return;

    float3 center = 0.0;
    float radius = 0.0;
    if (!virtual_geometry_visible(node, model, radius_scale, center, radius)) return;

    uint parent_index = node.parent_index;
    [loop]
    for (uint step = 0U; step < node_count && parent_index != 0xFFFFFFFFU; ++step) {
        if (parent_index >= candidate.node_index || parent_index >= node_count) return;
        const VirtualGeometryNode parent = virtual_geometry_nodes[parent_index];
        if (parent.mesh_index != candidate.mesh_index || parent.child_count == 0U) return;
        float3 parent_center = 0.0;
        float parent_radius = 0.0;
        if (!virtual_geometry_visible(parent, model, radius_scale, parent_center, parent_radius)) return;
        if (virtual_geometry_projected_error(parent, parent_center, parent_radius, radius_scale) <=
            max_projected_error_pixels) {
            return;
        }
        parent_index = parent.parent_index;
    }
    if (parent_index != 0xFFFFFFFFU) return;

    if (node.child_count != 0U &&
        virtual_geometry_projected_error(node, center, radius, radius_scale) >
            max_projected_error_pixels) {
        return;
    }

    uint node_output_index = 0U;
    InterlockedAdd(selected_node_count[0], 1U, node_output_index);
    selected_node_requests[node_output_index] = (VirtualGeometryNodeRequest) {
        candidate.scene_instance_index,
        candidate.mesh_index,
        candidate.local_node_index,
        0U
    };

    if (node.child_count == 0U && node.page_count == 1U) {
        uint page_output_index = 0U;
        InterlockedAdd(page_request_count[0], 1U, page_output_index);
        page_requests[page_output_index] = (VirtualGeometryPageRequest) {
            candidate.scene_instance_index,
            candidate.mesh_index,
            candidate.local_node_index,
            node.page_offset
        };
    }
}
)";

inline constexpr std::string_view gpu_virtual_geometry_metal_source = R"(
#include <metal_stdlib>
using namespace metal;

struct VirtualGeometryFrame {
    float4 frustum_planes[6];
    float projection_scale;
    float max_projected_error_pixels;
    uint candidate_count;
    uint node_count;
    uint instance_count;
    uint3 padding;
};

struct VirtualGeometryNode {
    float4 bounds_center_radius;
    float geometric_error;
    uint parent_index;
    uint first_child;
    uint child_count;
    uint page_offset;
    uint page_count;
    uint depth;
    uint mesh_index;
    uint local_node_index;
    uint reserved[3];
};

struct VirtualGeometryCandidate {
    uint scene_instance_index;
    uint node_index;
    uint mesh_index;
    uint local_node_index;
};

struct VirtualGeometryNodeRequest {
    uint scene_instance_index;
    uint mesh_index;
    uint node_index;
    uint reserved;
};

struct VirtualGeometryPageRequest {
    uint scene_instance_index;
    uint mesh_index;
    uint node_index;
    uint page_index;
};

float virtual_geometry_radius_scale(float4x4 model) {
    const float3 column0 = (model * float4(1.0f, 0.0f, 0.0f, 0.0f)).xyz;
    const float3 column1 = (model * float4(0.0f, 1.0f, 0.0f, 0.0f)).xyz;
    const float3 column2 = (model * float4(0.0f, 0.0f, 1.0f, 0.0f)).xyz;
    const float one_norm = max(
        dot(abs(column0), float3(1.0f)),
        max(dot(abs(column1), float3(1.0f)), dot(abs(column2), float3(1.0f)))
    );
    const float infinity_norm = max(
        abs(column0.x) + abs(column1.x) + abs(column2.x),
        max(
            abs(column0.y) + abs(column1.y) + abs(column2.y),
            abs(column0.z) + abs(column1.z) + abs(column2.z)
        )
    );
    return sqrt(max(0.0f, one_norm * infinity_norm));
}

bool virtual_geometry_visible(
    VirtualGeometryNode node,
    float4x4 model,
    float radius_scale,
    constant VirtualGeometryFrame& frame,
    thread float3& center,
    thread float& radius
) {
    center = (model * float4(node.bounds_center_radius.xyz, 1.0f)).xyz;
    radius = node.bounds_center_radius.w * radius_scale;
    bool visible = all(isfinite(center)) && isfinite(radius) && radius >= 0.0f;
    for (uint plane_index = 0U; plane_index < 6U; ++plane_index) {
        const float distance = dot(frame.frustum_planes[plane_index].xyz, center) +
            frame.frustum_planes[plane_index].w;
        visible = visible && distance >= -radius;
    }
    return visible;
}

float virtual_geometry_projected_error(
    VirtualGeometryNode node,
    float3 center,
    float radius,
    float radius_scale,
    constant VirtualGeometryFrame& frame
) {
    const float geometric_error = node.geometric_error * radius_scale;
    if (geometric_error <= 0.0f) return 0.0f;
    const float surface_distance = length(center) - radius;
    if (!isfinite(surface_distance) || surface_distance <= 1.0e-6f) {
        return numeric_limits<float>::max();
    }
    return geometric_error * frame.projection_scale / surface_distance;
}

kernel void vice_runtime_virtual_geometry_select_cs(
    constant VirtualGeometryFrame& frame [[buffer(0)]],
    device VirtualGeometryNode* nodes [[buffer(1)]],
    device VirtualGeometryCandidate* candidates [[buffer(2)]],
    device float4x4* camera_relative_models [[buffer(3)]],
    device atomic_uint* selected_node_count [[buffer(4)]],
    device VirtualGeometryNodeRequest* selected_node_requests [[buffer(5)]],
    device atomic_uint* page_request_count [[buffer(6)]],
    device VirtualGeometryPageRequest* page_requests [[buffer(7)]],
    uint3 dispatch_id [[thread_position_in_grid]]) {
    const uint candidate_index = dispatch_id.x;
    if (candidate_index >= frame.candidate_count) return;

    const VirtualGeometryCandidate candidate = candidates[candidate_index];
    if (candidate.scene_instance_index >= frame.instance_count || candidate.node_index >= frame.node_count) return;
    const VirtualGeometryNode node = nodes[candidate.node_index];
    if (node.mesh_index != candidate.mesh_index || node.local_node_index != candidate.local_node_index) return;

    const float4x4 model = camera_relative_models[candidate.scene_instance_index];
    const float radius_scale = virtual_geometry_radius_scale(model);
    if (!isfinite(radius_scale) || radius_scale < 0.0f) return;

    float3 center = float3(0.0f);
    float radius = 0.0f;
    if (!virtual_geometry_visible(node, model, radius_scale, frame, center, radius)) return;

    uint parent_index = node.parent_index;
    for (uint step = 0U; step < frame.node_count && parent_index != 0xFFFFFFFFU; ++step) {
        if (parent_index >= candidate.node_index || parent_index >= frame.node_count) return;
        const VirtualGeometryNode parent = nodes[parent_index];
        if (parent.mesh_index != candidate.mesh_index || parent.child_count == 0U) return;
        float3 parent_center = float3(0.0f);
        float parent_radius = 0.0f;
        if (!virtual_geometry_visible(parent, model, radius_scale, frame, parent_center, parent_radius)) return;
        if (virtual_geometry_projected_error(parent, parent_center, parent_radius, radius_scale, frame) <=
            frame.max_projected_error_pixels) {
            return;
        }
        parent_index = parent.parent_index;
    }
    if (parent_index != 0xFFFFFFFFU) return;

    if (node.child_count != 0U &&
        virtual_geometry_projected_error(node, center, radius, radius_scale, frame) >
            frame.max_projected_error_pixels) {
        return;
    }

    const uint node_output_index = atomic_fetch_add_explicit(
        &selected_node_count[0], 1U, memory_order_relaxed
    );
    selected_node_requests[node_output_index] = VirtualGeometryNodeRequest{
        candidate.scene_instance_index,
        candidate.mesh_index,
        candidate.local_node_index,
        0U
    };

    if (node.child_count == 0U && node.page_count == 1U) {
        const uint page_output_index = atomic_fetch_add_explicit(
            &page_request_count[0], 1U, memory_order_relaxed
        );
        page_requests[page_output_index] = VirtualGeometryPageRequest{
            candidate.scene_instance_index,
            candidate.mesh_index,
            candidate.local_node_index,
            node.page_offset
        };
    }
}
)";

} // namespace vice::runtime::detail
