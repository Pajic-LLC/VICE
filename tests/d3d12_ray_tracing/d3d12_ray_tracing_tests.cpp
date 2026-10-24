#include "ray_tracing.hpp"

#include <vice/render/ray_tracing.hpp>

#include <d3d12.h>
#include <wrl/client.h>

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace {

class TestBuffer final : public vice::render::Buffer {
public:
    TestBuffer(const std::uint64_t size, const vice::render::BufferUsage usage)
        : size_{size}, usage_{usage} {}

    [[nodiscard]] std::uint64_t size() const noexcept override { return size_; }
    [[nodiscard]] vice::render::BufferUsage usage() const noexcept override { return usage_; }
    [[nodiscard]] vice::render::MemoryLocation memory_location() const noexcept override {
        return vice::render::MemoryLocation::gpu_only;
    }
    bool write(std::uint64_t, std::span<const std::byte>) override { return false; }
    bool read(std::uint64_t, std::span<std::byte>) override { return false; }

private:
    std::uint64_t size_ = 0U;
    vice::render::BufferUsage usage_ = vice::render::BufferUsage::none;
};

class TestAccelerationStructure final : public vice::render::AccelerationStructure {
public:
    TestAccelerationStructure(
        const vice::render::AccelerationStructureType type,
        const std::uint64_t size
    ) : type_{type}, size_{size} {}

    [[nodiscard]] vice::render::AccelerationStructureType type() const noexcept override {
        return type_;
    }
    [[nodiscard]] std::uint64_t size() const noexcept override { return size_; }

private:
    vice::render::AccelerationStructureType type_;
    std::uint64_t size_ = 0U;
};

[[nodiscard]] bool native_flag_set(
    const D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS value,
    const D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS flag
) noexcept {
    return (static_cast<UINT>(value) & static_cast<UINT>(flag)) != 0U;
}

} // namespace

int main() {
    using namespace vice::render;
    using namespace vice::render::d3d12;
    using Microsoft::WRL::ComPtr;

    TestBuffer vertices{4096U, BufferUsage::vertex};
    TestBuffer indices{2048U, BufferUsage::index};

    const AccelerationStructureTriangleGeometryDesc geometry{
        .vertex_buffer = &vertices,
        .vertex_offset = 64U,
        .vertex_count = 32U,
        .vertex_stride = 32U,
        .vertex_format = VertexFormat::float32x3,
        .index_buffer = &indices,
        .index_offset = 16U,
        .index_count = 48U,
        .index_format = IndexFormat::uint32,
        .opaque = true,
    };
    const std::array geometries{geometry};
    const AccelerationStructureBuildDesc blas_desc{
        .type = AccelerationStructureType::bottom_level,
        .flags = AccelerationStructureBuildFlags::allow_update |
            AccelerationStructureBuildFlags::prefer_fast_trace,
        .geometries = geometries,
        .instances = {},
    };

    const auto buffer_address = [&](const Buffer& buffer)
        -> std::optional<D3D12_GPU_VIRTUAL_ADDRESS> {
        if (&buffer == &vertices) return 0x100000U;
        if (&buffer == &indices) return 0x200000U;
        return std::nullopt;
    };

    const auto blas_plan = build_acceleration_structure_plan(blas_desc, buffer_address);
    assert(blas_plan);
    assert(blas_plan.type == AccelerationStructureType::bottom_level);
    assert(blas_plan.geometries.size() == 1U);
    assert(blas_plan.instances.empty());
    assert(native_flag_set(
        blas_plan.flags,
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE
    ));
    assert(native_flag_set(
        blas_plan.flags,
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE
    ));

    const auto& native_geometry = blas_plan.geometries.front();
    assert(native_geometry.Type == D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES);
    assert(native_geometry.Flags == D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE);
    assert(native_geometry.Triangles.VertexFormat == DXGI_FORMAT_R32G32B32_FLOAT);
    assert(native_geometry.Triangles.IndexFormat == DXGI_FORMAT_R32_UINT);
    assert(native_geometry.Triangles.VertexBuffer.StartAddress == 0x100000U + geometry.vertex_offset);
    assert(native_geometry.Triangles.IndexBuffer == 0x200000U + geometry.index_offset);
    assert(native_geometry.Triangles.VertexBuffer.StrideInBytes == geometry.vertex_stride);
    assert(native_geometry.Triangles.VertexCount == geometry.vertex_count);
    assert(native_geometry.Triangles.IndexCount == geometry.index_count);

    const auto blas_inputs = blas_plan.inputs();
    assert(blas_inputs.Type == D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL);
    assert(blas_inputs.DescsLayout == D3D12_ELEMENTS_LAYOUT_ARRAY);
    assert(blas_inputs.NumDescs == 1U);
    assert(blas_inputs.pGeometryDescs == blas_plan.geometries.data());

    const auto missing_buffer_plan = build_acceleration_structure_plan(
        blas_desc,
        [](const Buffer&) -> std::optional<D3D12_GPU_VIRTUAL_ADDRESS> {
            return std::nullopt;
        }
    );
    assert(!missing_buffer_plan);

    TestAccelerationStructure bottom_level{AccelerationStructureType::bottom_level, 8192U};
    const AccelerationStructureInstanceDesc instance{
        .bottom_level = &bottom_level,
        .transform = {
            1.0F, 0.0F, 0.0F, 4.0F,
            0.0F, 1.0F, 0.0F, 5.0F,
            0.0F, 0.0F, 1.0F, 6.0F,
        },
        .instance_id = 17U,
        .mask = 0x5AU,
    };
    const std::array instances{instance};
    const AccelerationStructureBuildDesc tlas_desc{
        .type = AccelerationStructureType::top_level,
        .flags = AccelerationStructureBuildFlags::prefer_fast_build,
        .geometries = {},
        .instances = instances,
    };

    const auto as_address = [&](const AccelerationStructure& structure)
        -> std::optional<D3D12_GPU_VIRTUAL_ADDRESS> {
        if (&structure == &bottom_level) return 0x300000U;
        return std::nullopt;
    };
    const auto tlas_plan = build_acceleration_structure_plan(tlas_desc, {}, as_address);
    assert(tlas_plan);
    assert(tlas_plan.type == AccelerationStructureType::top_level);
    assert(tlas_plan.geometries.empty());
    assert(tlas_plan.instances.size() == 1U);
    assert(native_flag_set(
        tlas_plan.flags,
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD
    ));

    const auto& native_instance = tlas_plan.instances.front();
    assert(native_instance.InstanceID == 17U);
    assert(native_instance.InstanceMask == 0x5AU);
    assert(native_instance.InstanceContributionToHitGroupIndex == 0U);
    assert(native_instance.AccelerationStructure == 0x300000U);
    assert(native_instance.Transform[0][3] == 4.0F);
    assert(native_instance.Transform[1][3] == 5.0F);
    assert(native_instance.Transform[2][3] == 6.0F);

    constexpr D3D12_GPU_VIRTUAL_ADDRESS instance_buffer_address = 0x400000U;
    const auto tlas_inputs = tlas_plan.inputs(instance_buffer_address);
    assert(tlas_inputs.Type == D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL);
    assert(tlas_inputs.DescsLayout == D3D12_ELEMENTS_LAYOUT_ARRAY);
    assert(tlas_inputs.NumDescs == 1U);
    assert(tlas_inputs.InstanceDescs == instance_buffer_address);

    ComPtr<ID3D12Device> device;
    const HRESULT device_result = D3D12CreateDevice(
        nullptr,
        D3D_FEATURE_LEVEL_11_0,
        IID_PPV_ARGS(&device)
    );
    assert(SUCCEEDED(device_result));
    assert(device != nullptr);

    const bool dxr_supported = supports_ray_tracing(*device);
    const auto sizes = query_acceleration_structure_build_sizes(*device, blas_plan);
    if (dxr_supported) {
        assert(sizes);
        assert(is_valid(sizes.sizes));
    } else {
        assert(!sizes);
    }

    return 0;
}
