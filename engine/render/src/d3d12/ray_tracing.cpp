#include "ray_tracing.hpp"

#include <vice/render/mesh_input.hpp>

#include <wrl/client.h>

#include <cstdint>
#include <cstring>
#include <limits>

namespace vice::render::d3d12 {
namespace {

using Microsoft::WRL::ComPtr;

[[nodiscard]] D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS native_flags(
    const AccelerationStructureBuildFlags flags
) noexcept {
    UINT result = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE;
    if (has_flag(flags, AccelerationStructureBuildFlags::allow_update)) {
        result |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE;
    }
    if (has_flag(flags, AccelerationStructureBuildFlags::prefer_fast_trace)) {
        result |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
    }
    if (has_flag(flags, AccelerationStructureBuildFlags::prefer_fast_build)) {
        result |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD;
    }
    if (has_flag(flags, AccelerationStructureBuildFlags::minimize_memory)) {
        result |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_MINIMIZE_MEMORY;
    }
    return static_cast<D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS>(result);
}

[[nodiscard]] DXGI_FORMAT native_index_format(const IndexFormat format) noexcept {
    return format == IndexFormat::uint16 ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT;
}

[[nodiscard]] bool add_address_offset(
    const D3D12_GPU_VIRTUAL_ADDRESS address,
    const std::uint64_t offset,
    D3D12_GPU_VIRTUAL_ADDRESS& result
) noexcept {
    if (address == 0U || address > std::numeric_limits<D3D12_GPU_VIRTUAL_ADDRESS>::max() - offset) {
        return false;
    }
    result = address + offset;
    return result != 0U;
}

} // namespace

D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS
AccelerationStructureBuildPlan::inputs(
    const D3D12_GPU_VIRTUAL_ADDRESS instance_buffer
) const noexcept {
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS result{};
    result.Flags = flags;
    result.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;

    switch (type) {
    case AccelerationStructureType::bottom_level:
        result.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
        result.NumDescs = static_cast<UINT>(geometries.size());
        result.pGeometryDescs = geometries.data();
        break;
    case AccelerationStructureType::top_level:
        result.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
        result.NumDescs = static_cast<UINT>(instances.size());
        result.InstanceDescs = instance_buffer;
        break;
    }
    return result;
}

bool supports_ray_tracing(ID3D12Device& device) noexcept {
    D3D12_FEATURE_DATA_D3D12_OPTIONS5 options{};
    if (FAILED(device.CheckFeatureSupport(
        D3D12_FEATURE_D3D12_OPTIONS5,
        &options,
        sizeof(options)
    ))) {
        return false;
    }
    return options.RaytracingTier != D3D12_RAYTRACING_TIER_NOT_SUPPORTED;
}

AccelerationStructureBuildPlan build_acceleration_structure_plan(
    const AccelerationStructureBuildDesc& desc,
    const BufferGpuAddressResolver& buffer_address,
    const AccelerationStructureGpuAddressResolver& acceleration_structure_address
) {
    AccelerationStructureBuildPlan result;
    result.type = desc.type;
    result.flags = native_flags(desc.flags);

    if (!is_valid(desc)) {
        result.diagnostics = "Invalid VICE acceleration-structure build description.";
        return result;
    }

    if (desc.type == AccelerationStructureType::bottom_level) {
        if (!buffer_address) {
            result.diagnostics = "D3D12 BLAS planning requires a buffer GPU-address resolver.";
            return result;
        }
        if (desc.geometries.size() > std::numeric_limits<UINT>::max()) {
            result.diagnostics = "D3D12 BLAS geometry count exceeds the native limit.";
            return result;
        }

        result.geometries.reserve(desc.geometries.size());
        for (const auto& geometry : desc.geometries) {
            const auto vertex_base = buffer_address(*geometry.vertex_buffer);
            const auto index_base = buffer_address(*geometry.index_buffer);
            if (!vertex_base.has_value() || !index_base.has_value()) {
                result.diagnostics = "D3D12 could not resolve a BLAS source buffer GPU address.";
                result.geometries.clear();
                return result;
            }

            D3D12_GPU_VIRTUAL_ADDRESS vertex_address = 0U;
            D3D12_GPU_VIRTUAL_ADDRESS index_address = 0U;
            if (!add_address_offset(*vertex_base, geometry.vertex_offset, vertex_address) ||
                !add_address_offset(*index_base, geometry.index_offset, index_address)) {
                result.diagnostics = "D3D12 BLAS source buffer address overflowed.";
                result.geometries.clear();
                return result;
            }

            D3D12_RAYTRACING_GEOMETRY_DESC native{};
            native.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
            native.Flags = geometry.opaque
                ? D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE
                : D3D12_RAYTRACING_GEOMETRY_FLAG_NONE;
            native.Triangles.Transform3x4 = 0U;
            native.Triangles.IndexFormat = native_index_format(geometry.index_format);
            native.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
            native.Triangles.IndexCount = geometry.index_count;
            native.Triangles.VertexCount = geometry.vertex_count;
            native.Triangles.IndexBuffer = index_address;
            native.Triangles.VertexBuffer.StartAddress = vertex_address;
            native.Triangles.VertexBuffer.StrideInBytes = geometry.vertex_stride;
            result.geometries.push_back(native);
        }
        return result;
    }

    if (!acceleration_structure_address) {
        result.diagnostics = "D3D12 TLAS planning requires an acceleration-structure GPU-address resolver.";
        return result;
    }
    if (desc.instances.size() > std::numeric_limits<UINT>::max()) {
        result.diagnostics = "D3D12 TLAS instance count exceeds the native limit.";
        return result;
    }

    result.instances.reserve(desc.instances.size());
    for (const auto& instance : desc.instances) {
        const auto address = acceleration_structure_address(*instance.bottom_level);
        if (!address.has_value() || *address == 0U) {
            result.diagnostics = "D3D12 could not resolve a TLAS bottom-level GPU address.";
            result.instances.clear();
            return result;
        }

        D3D12_RAYTRACING_INSTANCE_DESC native{};
        std::memcpy(native.Transform, instance.transform.data(), sizeof(native.Transform));
        native.InstanceID = instance.instance_id;
        native.InstanceMask = instance.mask;
        native.InstanceContributionToHitGroupIndex = 0U;
        native.Flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
        native.AccelerationStructure = *address;
        result.instances.push_back(native);
    }
    return result;
}

AccelerationStructureBuildSizeResult query_acceleration_structure_build_sizes(
    ID3D12Device& device,
    const AccelerationStructureBuildPlan& plan
) {
    AccelerationStructureBuildSizeResult result;
    if (!plan) {
        result.diagnostics = plan.diagnostics.empty()
            ? "Invalid D3D12 acceleration-structure build plan."
            : plan.diagnostics;
        return result;
    }
    if (!supports_ray_tracing(device)) {
        result.diagnostics = "The active Direct3D 12 device does not support DXR acceleration structures.";
        return result;
    }

    ComPtr<ID3D12Device5> device5;
    if (FAILED(device.QueryInterface(IID_PPV_ARGS(&device5))) || device5 == nullptr) {
        result.diagnostics = "The active Direct3D 12 device does not expose the DXR device interface.";
        return result;
    }

    const auto inputs = plan.inputs();
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO native{};
    device5->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &native);

    result.sizes = AccelerationStructureBuildSizes{
        .result_size = native.ResultDataMaxSizeInBytes,
        .build_scratch_size = native.ScratchDataSizeInBytes,
        .update_scratch_size = native.UpdateScratchDataSizeInBytes,
    };
    if (!is_valid(result.sizes)) {
        result.diagnostics = "D3D12 returned invalid acceleration-structure prebuild sizes.";
        result.sizes = {};
    }
    return result;
}

} // namespace vice::render::d3d12
