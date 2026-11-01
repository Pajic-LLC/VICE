#pragma once

#include <vice/render/rhi.hpp>

#include <array>
#include <string_view>

namespace vice::runtime::detail {

inline constexpr std::array gpu_batch_compact_resources{
    render::ShaderResourceBindingDesc{render::ShaderRegisterClass::constant_buffer, 0U, 0U, 1U, false},
    render::ShaderResourceBindingDesc{render::ShaderRegisterClass::unordered_access, 1U, 0U, 1U, false},
    render::ShaderResourceBindingDesc{render::ShaderRegisterClass::unordered_access, 2U, 0U, 1U, false},
    render::ShaderResourceBindingDesc{render::ShaderRegisterClass::unordered_access, 3U, 0U, 1U, false},
    render::ShaderResourceBindingDesc{render::ShaderRegisterClass::unordered_access, 4U, 0U, 1U, false},
    render::ShaderResourceBindingDesc{render::ShaderRegisterClass::unordered_access, 5U, 0U, 1U, false},
    render::ShaderResourceBindingDesc{render::ShaderRegisterClass::unordered_access, 6U, 0U, 1U, false},
};

inline constexpr std::array gpu_batch_finalize_resources{
    render::ShaderResourceBindingDesc{render::ShaderRegisterClass::constant_buffer, 0U, 0U, 1U, false},
    render::ShaderResourceBindingDesc{render::ShaderRegisterClass::unordered_access, 7U, 0U, 1U, false},
    render::ShaderResourceBindingDesc{render::ShaderRegisterClass::unordered_access, 8U, 0U, 1U, false},
    render::ShaderResourceBindingDesc{render::ShaderRegisterClass::unordered_access, 9U, 0U, 1U, false},
};

inline constexpr render::ShaderBindingKey gpu_batch_constants_binding{
    render::ShaderRegisterClass::constant_buffer, 0U, 0U, 0U
};
inline constexpr render::ShaderBindingKey gpu_batch_visible_count_binding{
    render::ShaderRegisterClass::unordered_access, 1U, 0U, 0U
};
inline constexpr render::ShaderBindingKey gpu_batch_visible_indices_binding{
    render::ShaderRegisterClass::unordered_access, 2U, 0U, 0U
};
inline constexpr render::ShaderBindingKey gpu_batch_assignments_binding{
    render::ShaderRegisterClass::unordered_access, 3U, 0U, 0U
};
inline constexpr render::ShaderBindingKey gpu_batch_metadata_binding{
    render::ShaderRegisterClass::unordered_access, 4U, 0U, 0U
};
inline constexpr render::ShaderBindingKey gpu_batch_counts_binding{
    render::ShaderRegisterClass::unordered_access, 5U, 0U, 0U
};
inline constexpr render::ShaderBindingKey gpu_batch_compacted_binding{
    render::ShaderRegisterClass::unordered_access, 6U, 0U, 0U
};

inline constexpr render::ShaderBindingKey gpu_batch_finalize_metadata_binding{
    render::ShaderRegisterClass::unordered_access, 7U, 0U, 0U
};
inline constexpr render::ShaderBindingKey gpu_batch_finalize_counts_binding{
    render::ShaderRegisterClass::unordered_access, 8U, 0U, 0U
};
inline constexpr render::ShaderBindingKey gpu_batch_indirect_binding{
    render::ShaderRegisterClass::unordered_access, 9U, 0U, 0U
};

inline constexpr std::string_view gpu_batch_hlsl_source = R"(
cbuffer BatchConstants : register(b0) {
    uint instance_count;
    uint batch_count;
    uint visible_capacity;
    uint batch_padding;
};

struct BatchMetadata {
    uint index_count;
    uint first_index;
    int vertex_offset;
    uint output_offset;
    uint capacity;
    uint padding0;
    uint padding1;
    uint padding2;
};

RWStructuredBuffer<uint> visible_count : register(u1);
RWStructuredBuffer<uint> visible_indices : register(u2);
RWStructuredBuffer<uint> instance_batches : register(u3);
RWStructuredBuffer<BatchMetadata> batch_metadata : register(u4);
RWStructuredBuffer<uint> batch_counts : register(u5);
RWStructuredBuffer<uint> compacted_instances : register(u6);

[numthreads(64, 1, 1)]
void vice_runtime_batch_compact_cs(uint3 dispatch_id : SV_DispatchThreadID) {
    const uint visible_slot = dispatch_id.x;
    if (visible_slot >= visible_capacity || visible_slot >= visible_count[0]) return;
    const uint instance_index = visible_indices[visible_slot];
    if (instance_index >= instance_count) return;
    const uint batch_index = instance_batches[instance_index];
    if (batch_index >= batch_count) return;
    const BatchMetadata metadata = batch_metadata[batch_index];
    uint local_index = 0U;
    InterlockedAdd(batch_counts[batch_index], 1U, local_index);
    if (local_index < metadata.capacity) {
        compacted_instances[metadata.output_offset + local_index] = instance_index;
    }
}

RWStructuredBuffer<BatchMetadata> finalize_metadata : register(u7);
RWStructuredBuffer<uint> finalize_counts : register(u8);
RWStructuredBuffer<uint> indirect_words : register(u9);

[numthreads(64, 1, 1)]
void vice_runtime_batch_finalize_cs(uint3 dispatch_id : SV_DispatchThreadID) {
    const uint batch_index = dispatch_id.x;
    if (batch_index >= batch_count) return;
    const BatchMetadata metadata = finalize_metadata[batch_index];
    const uint word = batch_index * 5U;
    indirect_words[word + 0U] = metadata.index_count;
    indirect_words[word + 1U] = min(finalize_counts[batch_index], metadata.capacity);
    indirect_words[word + 2U] = metadata.first_index;
    indirect_words[word + 3U] = asuint(metadata.vertex_offset);
    indirect_words[word + 4U] = 0U;
}
)";

inline constexpr std::string_view gpu_batch_metal_source = R"(
#include <metal_stdlib>
using namespace metal;

struct BatchConstants {
    uint instance_count;
    uint batch_count;
    uint visible_capacity;
    uint batch_padding;
};

struct BatchMetadata {
    uint index_count;
    uint first_index;
    int vertex_offset;
    uint output_offset;
    uint capacity;
    uint padding0;
    uint padding1;
    uint padding2;
};

kernel void vice_runtime_batch_compact_cs(
    constant BatchConstants& constants [[buffer(0)]],
    device uint* visible_count [[buffer(1)]],
    device uint* visible_indices [[buffer(2)]],
    device uint* instance_batches [[buffer(3)]],
    device BatchMetadata* batch_metadata [[buffer(4)]],
    device atomic_uint* batch_counts [[buffer(5)]],
    device uint* compacted_instances [[buffer(6)]],
    uint3 dispatch_id [[thread_position_in_grid]]) {
    const uint visible_slot = dispatch_id.x;
    if (visible_slot >= constants.visible_capacity || visible_slot >= visible_count[0]) return;
    const uint instance_index = visible_indices[visible_slot];
    if (instance_index >= constants.instance_count) return;
    const uint batch_index = instance_batches[instance_index];
    if (batch_index >= constants.batch_count) return;
    const BatchMetadata metadata = batch_metadata[batch_index];
    const uint local_index = atomic_fetch_add_explicit(
        &batch_counts[batch_index], 1U, memory_order_relaxed
    );
    if (local_index < metadata.capacity) {
        compacted_instances[metadata.output_offset + local_index] = instance_index;
    }
}

kernel void vice_runtime_batch_finalize_cs(
    constant BatchConstants& constants [[buffer(0)]],
    device BatchMetadata* metadata [[buffer(7)]],
    device uint* counts [[buffer(8)]],
    device uint* indirect_words [[buffer(9)]],
    uint3 dispatch_id [[thread_position_in_grid]]) {
    const uint batch_index = dispatch_id.x;
    if (batch_index >= constants.batch_count) return;
    const BatchMetadata batch = metadata[batch_index];
    const uint word = batch_index * 5U;
    indirect_words[word + 0U] = batch.index_count;
    indirect_words[word + 1U] = min(counts[batch_index], batch.capacity);
    indirect_words[word + 2U] = batch.first_index;
    indirect_words[word + 3U] = as_type<uint>(batch.vertex_offset);
    indirect_words[word + 4U] = 0U;
}
)";

} // namespace vice::runtime::detail
