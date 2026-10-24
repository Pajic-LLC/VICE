#include "ray_tracing.hpp"

#import <Metal/Metal.h>

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>

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

[[nodiscard]] bool usage_set(
    const MTLAccelerationStructureUsage value,
    const MTLAccelerationStructureUsage flag
) noexcept {
    return (static_cast<NSUInteger>(value) & static_cast<NSUInteger>(flag)) != 0U;
}

} // namespace

int main() {
    using namespace vice::render;
    using namespace vice::render::metal;

    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    if (device == nil) return 0;

    TestBuffer vertices{4096U, BufferUsage::vertex};
    TestBuffer indices{2048U, BufferUsage::index};
    id<MTLBuffer> native_vertices = [device newBufferWithLength:vertices.size()
                                                       options:MTLResourceStorageModeShared];
    id<MTLBuffer> native_indices = [device newBufferWithLength:indices.size()
                                                      options:MTLResourceStorageModeShared];
    assert(native_vertices != nil);
    assert(native_indices != nil);

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

    const auto buffer_binding = [&](const Buffer& buffer) -> BufferBinding {
        if (&buffer == &vertices) return {.buffer = native_vertices, .offset = 8U};
        if (&buffer == &indices) return {.buffer = native_indices, .offset = 4U};
        return {};
    };

    const auto blas_plan = build_acceleration_structure_plan(
        device,
        blas_desc,
        buffer_binding
    );
    assert(blas_plan);
    assert(blas_plan.type == AccelerationStructureType::bottom_level);

    auto* primitive = (MTLPrimitiveAccelerationStructureDescriptor*)blas_plan.descriptor;
    assert(primitive != nil);
    assert(primitive.geometryDescriptors.count == 1U);
    assert(usage_set(primitive.usage, MTLAccelerationStructureUsageRefit));
    assert(!usage_set(primitive.usage, MTLAccelerationStructureUsagePreferFastBuild));

    auto* native_geometry =
        (MTLAccelerationStructureTriangleGeometryDescriptor*)primitive.geometryDescriptors[0];
    assert(native_geometry.vertexBuffer == native_vertices);
    assert(native_geometry.vertexBufferOffset == 8U + geometry.vertex_offset);
    assert(native_geometry.vertexStride == geometry.vertex_stride);
    assert(native_geometry.vertexFormat == MTLAttributeFormatFloat3);
    assert(native_geometry.indexBuffer == native_indices);
    assert(native_geometry.indexBufferOffset == 4U + geometry.index_offset);
    assert(native_geometry.indexType == MTLIndexTypeUInt32);
    assert(native_geometry.triangleCount == geometry.index_count / 3U);
    assert(native_geometry.opaque);

    const bool ray_tracing_supported = supports_ray_tracing(device);
    const auto blas_sizes = query_acceleration_structure_build_sizes(device, blas_plan);
    if (!ray_tracing_supported) {
        assert(!blas_sizes);
        assert(!allocate_acceleration_structure_resources(device, blas_plan, {
            .result_size = 4096U,
            .build_scratch_size = 2048U,
            .update_scratch_size = 1024U,
        }));
        return 0;
    }
    assert(blas_sizes);
    assert(is_valid(blas_sizes.sizes));

    auto blas_resources = allocate_acceleration_structure_resources(
        device,
        blas_plan,
        blas_sizes.sizes
    );
    auto updated_blas_resources = allocate_acceleration_structure_resources(
        device,
        blas_plan,
        blas_sizes.sizes
    );
    assert(blas_resources);
    assert(updated_blas_resources);
    assert(blas_resources.result.size >= blas_sizes.sizes.result_size);
    assert(blas_resources.scratch.length >= blas_sizes.sizes.build_scratch_size);

    id<MTLCommandQueue> queue = [device newCommandQueue];
    assert(queue != nil);
    id<MTLCommandBuffer> command_buffer = [queue commandBuffer];
    assert(command_buffer != nil);

    std::string diagnostics;
    assert(encode_acceleration_structure_build(
        command_buffer,
        blas_plan,
        blas_resources,
        false,
        nullptr,
        &diagnostics
    ));
    assert(diagnostics.empty());
    assert(encode_acceleration_structure_build(
        command_buffer,
        blas_plan,
        updated_blas_resources,
        true,
        &blas_resources,
        &diagnostics
    ));
    assert(diagnostics.empty());

    id<MTLAccelerationStructure> native_blas =
        [device newAccelerationStructureWithSize:blas_sizes.sizes.result_size];
    assert(native_blas != nil);

    TestAccelerationStructure bottom_level{
        AccelerationStructureType::bottom_level,
        blas_sizes.sizes.result_size
    };
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
        .flags = AccelerationStructureBuildFlags::allow_update |
            AccelerationStructureBuildFlags::prefer_fast_build,
        .geometries = {},
        .instances = instances,
    };

    const auto tlas_plan = build_acceleration_structure_plan(
        device,
        tlas_desc,
        {},
        [&](const AccelerationStructure& structure) -> id<MTLAccelerationStructure> {
            return &structure == &bottom_level ? native_blas : nil;
        }
    );
    assert(tlas_plan);
    assert(tlas_plan.type == AccelerationStructureType::top_level);
    assert(tlas_plan.instance_buffer != nil);

    auto* instanced = (MTLInstanceAccelerationStructureDescriptor*)tlas_plan.descriptor;
    assert(instanced != nil);
    assert(instanced.instanceDescriptorType == MTLAccelerationStructureInstanceDescriptorTypeUserID);
    assert(instanced.instanceCount == 1U);
    assert(instanced.instanceDescriptorStride == sizeof(MTLAccelerationStructureUserIDInstanceDescriptor));
    assert(instanced.instancedAccelerationStructures.count == 1U);
    assert(instanced.instancedAccelerationStructures[0] == native_blas);
    assert(usage_set(instanced.usage, MTLAccelerationStructureUsageRefit));
    assert(usage_set(instanced.usage, MTLAccelerationStructureUsagePreferFastBuild));

    const auto* native_instance = static_cast<const MTLAccelerationStructureUserIDInstanceDescriptor*>(
        [tlas_plan.instance_buffer contents]
    );
    assert(native_instance != nullptr);
    assert(native_instance->userID == 17U);
    assert(native_instance->mask == 0x5AU);
    assert(native_instance->accelerationStructureIndex == 0U);

    std::array<float, 12> matrix{};
    static_assert(sizeof(matrix) == sizeof(native_instance->transformationMatrix));
    std::memcpy(matrix.data(), &native_instance->transformationMatrix, sizeof(matrix));
    const std::array<float, 12> expected_matrix{
        1.0F, 0.0F, 0.0F,
        0.0F, 1.0F, 0.0F,
        0.0F, 0.0F, 1.0F,
        4.0F, 5.0F, 6.0F,
    };
    assert(matrix == expected_matrix);

    const auto tlas_sizes = query_acceleration_structure_build_sizes(device, tlas_plan);
    assert(tlas_sizes);
    assert(is_valid(tlas_sizes.sizes));

    const auto live_tlas_plan = build_acceleration_structure_plan(
        device,
        tlas_desc,
        {},
        [&](const AccelerationStructure& structure) -> id<MTLAccelerationStructure> {
            return &structure == &bottom_level ? blas_resources.result : nil;
        }
    );
    assert(live_tlas_plan);
    auto tlas_resources = allocate_acceleration_structure_resources(
        device,
        live_tlas_plan,
        tlas_sizes.sizes
    );
    auto updated_tlas_resources = allocate_acceleration_structure_resources(
        device,
        live_tlas_plan,
        tlas_sizes.sizes
    );
    assert(tlas_resources);
    assert(updated_tlas_resources);
    assert(encode_acceleration_structure_build(
        command_buffer,
        live_tlas_plan,
        tlas_resources,
        false,
        nullptr,
        &diagnostics
    ));
    assert(diagnostics.empty());

    auto moved_instance = instance;
    moved_instance.transform[3] = 8.0F;
    moved_instance.transform[7] = 9.0F;
    moved_instance.mask = 0x3CU;
    const std::array moved_instances{moved_instance};
    const AccelerationStructureBuildDesc moved_tlas_desc{
        .type = AccelerationStructureType::top_level,
        .flags = AccelerationStructureBuildFlags::allow_update |
            AccelerationStructureBuildFlags::prefer_fast_build,
        .geometries = {},
        .instances = moved_instances,
    };
    const auto moved_tlas_plan = build_acceleration_structure_plan(
        device,
        moved_tlas_desc,
        {},
        [&](const AccelerationStructure& structure) -> id<MTLAccelerationStructure> {
            return &structure == &bottom_level ? blas_resources.result : nil;
        }
    );
    assert(moved_tlas_plan);
    assert(encode_acceleration_structure_build(
        command_buffer,
        moved_tlas_plan,
        updated_tlas_resources,
        true,
        &tlas_resources,
        &diagnostics
    ));
    assert(diagnostics.empty());
    assert(updated_tlas_resources.instance_buffer == moved_tlas_plan.instance_buffer);

    const auto* moved_native_instance =
        static_cast<const MTLAccelerationStructureUserIDInstanceDescriptor*>(
            [updated_tlas_resources.instance_buffer contents]
        );
    assert(moved_native_instance != nullptr);
    assert(moved_native_instance->mask == 0x3CU);
    assert(moved_native_instance->userID == 17U);

    return 0;
}
