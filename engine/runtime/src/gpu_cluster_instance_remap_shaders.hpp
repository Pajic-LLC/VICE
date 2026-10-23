#pragma once

#include <vice/render/rhi.hpp>

#include <array>
#include <string_view>

namespace vice::runtime::detail {

inline constexpr std::array cluster_instance_remap_resources{
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

inline constexpr render::ShaderBindingKey cluster_instance_remap_constants_binding{
    render::ShaderRegisterClass::constant_buffer, 0U, 0U, 0U,
};
inline constexpr render::ShaderBindingKey cluster_instance_remap_compacted_binding{
    render::ShaderRegisterClass::unordered_access, 1U, 0U, 0U,
};
inline constexpr render::ShaderBindingKey cluster_instance_remap_candidates_binding{
    render::ShaderRegisterClass::unordered_access, 2U, 0U, 0U,
};
inline constexpr render::ShaderBindingKey cluster_instance_remap_output_binding{
    render::ShaderRegisterClass::unordered_access, 3U, 0U, 0U,
};

inline constexpr std::string_view cluster_instance_remap_hlsl_source = R"(
cbuffer ClusterInstanceRemapFrame : register(b0) {
    uint candidate_count;
    uint remap_count;
    uint remap_padding_0;
    uint remap_padding_1;
};

struct ClusterCandidate {
    uint scene_instance_index;
    uint mesh_index;
    uint meshlet_index;
    uint reserved;
};

RWStructuredBuffer<uint> compacted_candidate_indices : register(u1);
RWStructuredBuffer<ClusterCandidate> cluster_candidates : register(u2);
RWStructuredBuffer<uint> compacted_scene_instance_indices : register(u3);

[numthreads(64, 1, 1)]
void vice_runtime_cluster_instance_remap_cs(uint3 dispatch_id : SV_DispatchThreadID) {
    const uint output_index = dispatch_id.x;
    if (output_index >= remap_count) return;
    const uint candidate_index = compacted_candidate_indices[output_index];
    compacted_scene_instance_indices[output_index] = candidate_index < candidate_count
        ? cluster_candidates[candidate_index].scene_instance_index
        : 0U;
}
)";

inline constexpr std::string_view cluster_instance_remap_metal_source = R"(
#include <metal_stdlib>
using namespace metal;

struct ClusterInstanceRemapFrame {
    uint candidate_count;
    uint remap_count;
    uint padding_0;
    uint padding_1;
};

struct ClusterCandidate {
    uint scene_instance_index;
    uint mesh_index;
    uint meshlet_index;
    uint reserved;
};

kernel void vice_runtime_cluster_instance_remap_cs(
    constant ClusterInstanceRemapFrame& frame [[buffer(0)]],
    device uint* compacted_candidate_indices [[buffer(1)]],
    device ClusterCandidate* cluster_candidates [[buffer(2)]],
    device uint* compacted_scene_instance_indices [[buffer(3)]],
    uint3 dispatch_id [[thread_position_in_grid]]) {
    const uint output_index = dispatch_id.x;
    if (output_index >= frame.remap_count) return;
    const uint candidate_index = compacted_candidate_indices[output_index];
    compacted_scene_instance_indices[output_index] = candidate_index < frame.candidate_count
        ? cluster_candidates[candidate_index].scene_instance_index
        : 0U;
}
)";

} // namespace vice::runtime::detail
