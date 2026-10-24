#include <vice/render/ray_tracing.hpp>

#include <array>
#include <cassert>
#include <cstdint>
#include <memory>

int main() {
    using namespace vice::render;

    auto device = Device::create();
    if (device == nullptr) {
        return 0;
    }

    const DeviceCapabilities capabilities = device->capabilities();

    const BufferDesc vertex_desc{
        .size = 4096U,
        .usage = BufferUsage::vertex,
        .memory = MemoryLocation::gpu_only,
        .debug_name = "ray tracing public RHI vertices",
    };
    const BufferDesc index_desc{
        .size = 2048U,
        .usage = BufferUsage::index,
        .memory = MemoryLocation::gpu_only,
        .debug_name = "ray tracing public RHI indices",
    };
    auto vertices = device->create_buffer(vertex_desc);
    auto indices = device->create_buffer(index_desc);
    assert(vertices != nullptr);
    assert(indices != nullptr);

    const AccelerationStructureTriangleGeometryDesc geometry{
        .vertex_buffer = vertices.get(),
        .vertex_offset = 0U,
        .vertex_count = 24U,
        .vertex_stride = 32U,
        .vertex_format = VertexFormat::float32x3,
        .index_buffer = indices.get(),
        .index_offset = 0U,
        .index_count = 36U,
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
    assert(is_valid(blas_desc));

    AccelerationStructureBuildSizes blas_sizes{};
    const bool blas_sizes_available = query_acceleration_structure_build_sizes(
        *device,
        blas_desc,
        blas_sizes
    );
    if (!capabilities.ray_tracing) {
        assert(!blas_sizes_available);
        assert(create_acceleration_structure(*device, blas_desc, blas_sizes) == nullptr);
        return 0;
    }

    assert(blas_sizes_available);
    assert(is_valid(blas_sizes));
    auto blas = create_acceleration_structure(*device, blas_desc, blas_sizes);
    auto updated_blas = create_acceleration_structure(*device, blas_desc, blas_sizes);
    assert(blas != nullptr);
    assert(updated_blas != nullptr);

    const AccelerationStructureInstanceDesc instance{
        .bottom_level = blas.get(),
        .transform = {
            1.0F, 0.0F, 0.0F, 0.0F,
            0.0F, 1.0F, 0.0F, 0.0F,
            0.0F, 0.0F, 1.0F, 0.0F,
        },
        .instance_id = 17U,
        .mask = 0xFFU,
    };
    const std::array instances{instance};
    const AccelerationStructureBuildDesc tlas_desc{
        .type = AccelerationStructureType::top_level,
        .flags = AccelerationStructureBuildFlags::allow_update |
            AccelerationStructureBuildFlags::prefer_fast_trace,
        .geometries = {},
        .instances = instances,
    };
    assert(is_valid(tlas_desc));

    AccelerationStructureBuildSizes tlas_sizes{};
    assert(query_acceleration_structure_build_sizes(*device, tlas_desc, tlas_sizes));
    assert(is_valid(tlas_sizes));
    auto tlas = create_acceleration_structure(*device, tlas_desc, tlas_sizes);
    auto updated_tlas = create_acceleration_structure(*device, tlas_desc, tlas_sizes);
    assert(tlas != nullptr);
    assert(updated_tlas != nullptr);

    auto commands = device->create_command_list();
    assert(commands != nullptr);
    assert(build_acceleration_structure(*commands, *blas, blas_desc));
    assert(build_acceleration_structure(*commands, *updated_blas, blas_desc, blas.get()));
    assert(build_acceleration_structure(*commands, *tlas, tlas_desc));

    auto moved_instance = instance;
    moved_instance.transform[3] = 8.0F;
    moved_instance.transform[7] = 4.0F;
    const std::array moved_instances{moved_instance};
    const AccelerationStructureBuildDesc moved_tlas_desc{
        .type = AccelerationStructureType::top_level,
        .flags = AccelerationStructureBuildFlags::allow_update |
            AccelerationStructureBuildFlags::prefer_fast_trace,
        .geometries = {},
        .instances = moved_instances,
    };
    assert(build_acceleration_structure(
        *commands,
        *updated_tlas,
        moved_tlas_desc,
        tlas.get()
    ));

    return 0;
}
