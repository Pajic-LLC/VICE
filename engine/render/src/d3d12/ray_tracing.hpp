#pragma once

#include <vice/render/ray_tracing.hpp>

#include <d3d12.h>
#include <wrl/client.h>

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace vice::render::d3d12 {

using BufferGpuAddressResolver = std::function<std::optional<D3D12_GPU_VIRTUAL_ADDRESS>(const Buffer&)>;
using AccelerationStructureGpuAddressResolver =
    std::function<std::optional<D3D12_GPU_VIRTUAL_ADDRESS>(const AccelerationStructure&)>;

struct AccelerationStructureBuildPlan {
    AccelerationStructureType type = AccelerationStructureType::bottom_level;
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS flags =
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE;
    std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> geometries;
    std::vector<D3D12_RAYTRACING_INSTANCE_DESC> instances;
    std::string diagnostics;

    [[nodiscard]] explicit operator bool() const noexcept {
        return diagnostics.empty();
    }

    [[nodiscard]] D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs(
        D3D12_GPU_VIRTUAL_ADDRESS instance_buffer = 0U
    ) const noexcept;
};

struct AccelerationStructureBuildSizeResult {
    AccelerationStructureBuildSizes sizes{};
    std::string diagnostics;

    [[nodiscard]] explicit operator bool() const noexcept {
        return diagnostics.empty();
    }
};

struct AccelerationStructureResources {
    AccelerationStructureType type = AccelerationStructureType::bottom_level;
    AccelerationStructureBuildSizes sizes{};
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS flags =
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE;
    Microsoft::WRL::ComPtr<ID3D12Resource> result;
    Microsoft::WRL::ComPtr<ID3D12Resource> scratch;
    Microsoft::WRL::ComPtr<ID3D12Resource> instance_upload;
    std::string diagnostics;

    [[nodiscard]] explicit operator bool() const noexcept {
        return diagnostics.empty() && result != nullptr && scratch != nullptr;
    }
};

[[nodiscard]] bool supports_ray_tracing(ID3D12Device& device) noexcept;

[[nodiscard]] AccelerationStructureBuildPlan build_acceleration_structure_plan(
    const AccelerationStructureBuildDesc& desc,
    const BufferGpuAddressResolver& buffer_address,
    const AccelerationStructureGpuAddressResolver& acceleration_structure_address = {}
);

[[nodiscard]] AccelerationStructureBuildSizeResult query_acceleration_structure_build_sizes(
    ID3D12Device& device,
    const AccelerationStructureBuildPlan& plan
);

[[nodiscard]] AccelerationStructureResources allocate_acceleration_structure_resources(
    ID3D12Device& device,
    const AccelerationStructureBuildPlan& plan,
    const AccelerationStructureBuildSizes& sizes
);

[[nodiscard]] bool record_acceleration_structure_build(
    ID3D12GraphicsCommandList4& command_list,
    const AccelerationStructureBuildPlan& plan,
    AccelerationStructureResources& resources,
    bool update = false,
    const AccelerationStructureResources* source = nullptr,
    std::string* diagnostics = nullptr
);

} // namespace vice::render::d3d12
