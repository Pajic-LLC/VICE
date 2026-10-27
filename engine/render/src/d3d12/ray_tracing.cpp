#include "ray_tracing.hpp"

#include <vice/render/mesh_input.hpp>

#include <wrl/client.h>

#include <algorithm>
#include <cstddef>
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

[[nodiscard]] bool native_flag_set(
    const D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS value,
    const D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS flag
) noexcept {
    return (static_cast<UINT>(value) & static_cast<UINT>(flag)) != 0U;
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

[[nodiscard]] bool aligned_size(
    const std::uint64_t value,
    const std::uint64_t alignment,
    std::uint64_t& result
) noexcept {
    if (value == 0U || alignment == 0U ||
        value > std::numeric_limits<std::uint64_t>::max() - (alignment - 1U)) {
        return false;
    }
    result = (value + alignment - 1U) / alignment * alignment;
    return result >= value;
}

[[nodiscard]] ComPtr<ID3D12Resource> create_buffer_resource(
    ID3D12Device& device,
    const std::uint64_t size,
    const D3D12_HEAP_TYPE heap_type,
    const D3D12_RESOURCE_FLAGS flags,
    const D3D12_RESOURCE_STATES initial_state
) {
    if (size == 0U) return {};

    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = heap_type;
    heap.CreationNodeMask = 1U;
    heap.VisibleNodeMask = 1U;

    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Alignment = 0U;
    desc.Width = size;
    desc.Height = 1U;
    desc.DepthOrArraySize = 1U;
    desc.MipLevels = 1U;
    desc.Format = DXGI_FORMAT_UNKNOWN;
    desc.SampleDesc.Count = 1U;
    desc.SampleDesc.Quality = 0U;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.Flags = flags;

    ComPtr<ID3D12Resource> resource;
    if (FAILED(device.CreateCommittedResource(
        &heap,
        D3D12_HEAP_FLAG_NONE,
        &desc,
        initial_state,
        nullptr,
        IID_PPV_ARGS(&resource)
    ))) {
        return {};
    }
    return resource;
}

[[nodiscard]] std::uint64_t resource_size(ID3D12Resource* const resource) noexcept {
    if (resource == nullptr) return 0U;
    return resource->GetDesc().Width;
}

void set_diagnostics(std::string* const diagnostics, std::string message) {
    if (diagnostics != nullptr) *diagnostics = std::move(message);
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

AccelerationStructureResources allocate_acceleration_structure_resources(
    ID3D12Device& device,
    const AccelerationStructureBuildPlan& plan,
    const AccelerationStructureBuildSizes& sizes
) {
    AccelerationStructureResources result;
    result.type = plan.type;
    result.flags = plan.flags;
    result.sizes = sizes;

    if (!plan || !is_valid(sizes)) {
        result.diagnostics = "Invalid D3D12 acceleration-structure allocation request.";
        return result;
    }
    if (!supports_ray_tracing(device)) {
        result.diagnostics = "The active Direct3D 12 device does not support DXR acceleration structures.";
        return result;
    }

    std::uint64_t result_size = 0U;
    if (!aligned_size(
        sizes.result_size,
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT,
        result_size
    )) {
        result.diagnostics = "D3D12 acceleration-structure result size overflowed alignment.";
        return result;
    }

    const std::uint64_t requested_scratch = std::max(
        sizes.build_scratch_size,
        native_flag_set(plan.flags, D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE)
            ? sizes.update_scratch_size
            : 0U
    );
    std::uint64_t scratch_size = 0U;
    if (!aligned_size(
        requested_scratch,
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT,
        scratch_size
    )) {
        result.diagnostics = "D3D12 acceleration-structure scratch size overflowed alignment.";
        return result;
    }

    result.result = create_buffer_resource(
        device,
        result_size,
        D3D12_HEAP_TYPE_DEFAULT,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE
    );
    result.scratch = create_buffer_resource(
        device,
        scratch_size,
        D3D12_HEAP_TYPE_DEFAULT,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS
    );
    if (result.result == nullptr || result.scratch == nullptr) {
        result.diagnostics = "D3D12 could not allocate acceleration-structure result or scratch memory.";
        result.result.Reset();
        result.scratch.Reset();
        return result;
    }

    if (plan.type != AccelerationStructureType::top_level) return result;

    if (plan.instances.size() >
        std::numeric_limits<std::uint64_t>::max() / sizeof(D3D12_RAYTRACING_INSTANCE_DESC)) {
        result.diagnostics = "D3D12 TLAS instance upload size overflowed.";
        result.result.Reset();
        result.scratch.Reset();
        return result;
    }
    const std::uint64_t instance_bytes =
        static_cast<std::uint64_t>(plan.instances.size()) * sizeof(D3D12_RAYTRACING_INSTANCE_DESC);
    std::uint64_t instance_size = 0U;
    if (!aligned_size(
        instance_bytes,
        D3D12_RAYTRACING_INSTANCE_DESCS_BYTE_ALIGNMENT,
        instance_size
    )) {
        result.diagnostics = "D3D12 TLAS instance upload size overflowed alignment.";
        result.result.Reset();
        result.scratch.Reset();
        return result;
    }

    result.instance_upload = create_buffer_resource(
        device,
        instance_size,
        D3D12_HEAP_TYPE_UPLOAD,
        D3D12_RESOURCE_FLAG_NONE,
        D3D12_RESOURCE_STATE_GENERIC_READ
    );
    if (result.instance_upload == nullptr) {
        result.diagnostics = "D3D12 could not allocate the TLAS instance upload buffer.";
        result.result.Reset();
        result.scratch.Reset();
        return result;
    }

    void* mapped = nullptr;
    const D3D12_RANGE read_range{0U, 0U};
    if (FAILED(result.instance_upload->Map(0U, &read_range, &mapped)) || mapped == nullptr) {
        result.diagnostics = "D3D12 could not map the TLAS instance upload buffer.";
        result.result.Reset();
        result.scratch.Reset();
        result.instance_upload.Reset();
        return result;
    }
    std::memcpy(mapped, plan.instances.data(), static_cast<std::size_t>(instance_bytes));
    const D3D12_RANGE written_range{0U, static_cast<SIZE_T>(instance_bytes)};
    result.instance_upload->Unmap(0U, &written_range);
    return result;
}

bool record_acceleration_structure_build(
    ID3D12GraphicsCommandList4& command_list,
    const AccelerationStructureBuildPlan& plan,
    AccelerationStructureResources& resources,
    const bool update,
    const AccelerationStructureResources* const source,
    std::string* const diagnostics
) {
    if (diagnostics != nullptr) diagnostics->clear();
    if (!plan || !resources || plan.type != resources.type || !is_valid(resources.sizes)) {
        set_diagnostics(diagnostics, "Invalid D3D12 acceleration-structure build resources.");
        return false;
    }

    if (resource_size(resources.result.Get()) < resources.sizes.result_size) {
        set_diagnostics(diagnostics, "D3D12 acceleration-structure result memory is undersized.");
        return false;
    }

    const std::uint64_t required_scratch = update
        ? resources.sizes.update_scratch_size
        : resources.sizes.build_scratch_size;
    // Some DXR devices require no scratch for an update. The initial build
    // allocation still owns a valid scratch buffer; zero is not an undersize.
    if (resource_size(resources.scratch.Get()) < required_scratch) {
        set_diagnostics(diagnostics, "D3D12 acceleration-structure scratch memory is undersized.");
        return false;
    }

    D3D12_GPU_VIRTUAL_ADDRESS instance_address = 0U;
    if (plan.type == AccelerationStructureType::top_level) {
        if (resources.instance_upload == nullptr) {
            set_diagnostics(diagnostics, "D3D12 TLAS build is missing its instance upload buffer.");
            return false;
        }
        if (plan.instances.size() >
            std::numeric_limits<std::uint64_t>::max() / sizeof(D3D12_RAYTRACING_INSTANCE_DESC)) {
            set_diagnostics(diagnostics, "D3D12 TLAS instance upload size overflowed.");
            return false;
        }
        const std::uint64_t instance_bytes =
            static_cast<std::uint64_t>(plan.instances.size()) * sizeof(D3D12_RAYTRACING_INSTANCE_DESC);
        if (instance_bytes == 0U || resource_size(resources.instance_upload.Get()) < instance_bytes) {
            set_diagnostics(diagnostics, "D3D12 TLAS instance upload buffer is undersized.");
            return false;
        }
        void* mapped = nullptr;
        const D3D12_RANGE read_range{0U, 0U};
        if (FAILED(resources.instance_upload->Map(0U, &read_range, &mapped)) || mapped == nullptr) {
            set_diagnostics(diagnostics, "D3D12 could not map the TLAS instance upload buffer for build.");
            return false;
        }
        std::memcpy(mapped, plan.instances.data(), static_cast<std::size_t>(instance_bytes));
        const D3D12_RANGE written_range{0U, static_cast<SIZE_T>(instance_bytes)};
        resources.instance_upload->Unmap(0U, &written_range);

        instance_address = resources.instance_upload->GetGPUVirtualAddress();
        if (instance_address == 0U ||
            instance_address % D3D12_RAYTRACING_INSTANCE_DESCS_BYTE_ALIGNMENT != 0U) {
            set_diagnostics(diagnostics, "D3D12 TLAS instance upload address is invalid.");
            return false;
        }
    }

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC native{};
    native.Inputs = plan.inputs(instance_address);
    native.DestAccelerationStructureData = resources.result->GetGPUVirtualAddress();
    native.ScratchAccelerationStructureData = resources.scratch->GetGPUVirtualAddress();
    if (native.DestAccelerationStructureData == 0U || native.ScratchAccelerationStructureData == 0U) {
        set_diagnostics(diagnostics, "D3D12 acceleration-structure GPU addresses are invalid.");
        return false;
    }

    if (update) {
        if (!native_flag_set(
            plan.flags,
            D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE
        ) || source == nullptr || !*source || source->type != plan.type ||
            !native_flag_set(
                source->flags,
                D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE
            )) {
            set_diagnostics(diagnostics, "D3D12 acceleration-structure update was not enabled by the source build.");
            return false;
        }
        native.SourceAccelerationStructureData = source->result->GetGPUVirtualAddress();
        if (native.SourceAccelerationStructureData == 0U) {
            set_diagnostics(diagnostics, "D3D12 acceleration-structure update source address is invalid.");
            return false;
        }
        const UINT flags = static_cast<UINT>(native.Inputs.Flags) |
            D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE;
        native.Inputs.Flags =
            static_cast<D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS>(flags);
    }

    command_list.BuildRaytracingAccelerationStructure(&native, 0U, nullptr);

    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.UAV.pResource = resources.result.Get();
    command_list.ResourceBarrier(1U, &barrier);
    return true;
}

} // namespace vice::render::d3d12
