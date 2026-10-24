#include <vice/render/ray_tracing.hpp>
#include <vice/render/shader_asset.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <span>
#include <string>

#define CHECK(condition) do { \
    if (!(condition)) { \
        std::cerr << "CHECK failed at line " << __LINE__ << ": " #condition << '\n'; \
        return __LINE__; \
    } \
} while (false)

int main() {
#ifndef VICE_INLINE_RAY_QUERY_MANIFEST
    return 0;
#else
    using namespace vice::render;

    std::ifstream input{VICE_INLINE_RAY_QUERY_MANIFEST, std::ios::binary};
    CHECK(input.good());

    const std::string json{
        std::istreambuf_iterator<char>{input},
        std::istreambuf_iterator<char>{}
    };
    const CookedShaderManifest manifest = parse_cooked_shader_manifest(json);
    CHECK(manifest.valid);
    CHECK(manifest.stage == ShaderStage::compute);
    CHECK(manifest.entry_point == "vice_inline_ray_query_cs");
    CHECK(manifest.compute_thread_group_size.has_value());
    CHECK(manifest.compute_thread_group_size->x == 1U);
    CHECK(manifest.compute_thread_group_size->y == 1U);
    CHECK(manifest.compute_thread_group_size->z == 1U);

    const auto tlas_resource = std::find_if(
        manifest.resources.begin(),
        manifest.resources.end(),
        [](const ShaderResourceBindingDesc& resource) {
            return resource.register_class == ShaderRegisterClass::shader_resource &&
                resource.binding == 0U && resource.space == 0U;
        }
    );
    CHECK(tlas_resource != manifest.resources.end());
    CHECK(tlas_resource->kind == ShaderResourceKind::acceleration_structure);
    CHECK(tlas_resource->count == 1U);
    CHECK(!tlas_resource->unbounded);

    const auto output_resource = std::find_if(
        manifest.resources.begin(),
        manifest.resources.end(),
        [](const ShaderResourceBindingDesc& resource) {
            return resource.register_class == ShaderRegisterClass::unordered_access &&
                resource.binding == 0U && resource.space == 0U;
        }
    );
    CHECK(output_resource != manifest.resources.end());
    CHECK(output_resource->kind == ShaderResourceKind::generic);
    CHECK(output_resource->count == 1U);
    CHECK(!output_resource->unbounded);

    auto device = Device::create();
    CHECK(device != nullptr);
    if (!device->capabilities().ray_tracing) {
        return 0;
    }

    constexpr std::array<float, 9> triangle_vertices{
        -1.0F, -1.0F, 5.0F,
         1.0F, -1.0F, 5.0F,
         0.0F,  1.0F, 5.0F,
    };
    constexpr std::array<std::uint32_t, 3> triangle_indices{0U, 1U, 2U};
    constexpr std::uint64_t vertex_bytes = sizeof(triangle_vertices);
    constexpr std::uint64_t index_bytes = sizeof(triangle_indices);

    auto vertex_upload = device->create_buffer(BufferDesc{
        .size = vertex_bytes,
        .usage = BufferUsage::copy_source,
        .memory = MemoryLocation::cpu_to_gpu,
        .debug_name = "inline ray query vertex upload",
    });
    auto index_upload = device->create_buffer(BufferDesc{
        .size = index_bytes,
        .usage = BufferUsage::copy_source,
        .memory = MemoryLocation::cpu_to_gpu,
        .debug_name = "inline ray query index upload",
    });
    auto vertices = device->create_buffer(BufferDesc{
        .size = vertex_bytes,
        .usage = BufferUsage::vertex | BufferUsage::copy_destination,
        .memory = MemoryLocation::gpu_only,
        .debug_name = "inline ray query vertices",
    });
    auto indices = device->create_buffer(BufferDesc{
        .size = index_bytes,
        .usage = BufferUsage::index | BufferUsage::copy_destination,
        .memory = MemoryLocation::gpu_only,
        .debug_name = "inline ray query indices",
    });
    CHECK(vertex_upload != nullptr);
    CHECK(index_upload != nullptr);
    CHECK(vertices != nullptr);
    CHECK(indices != nullptr);
    CHECK(vertex_upload->write(0U, std::as_bytes(std::span{triangle_vertices})));
    CHECK(index_upload->write(0U, std::as_bytes(std::span{triangle_indices})));

    const AccelerationStructureTriangleGeometryDesc geometry{
        .vertex_buffer = vertices.get(),
        .vertex_offset = 0U,
        .vertex_count = 3U,
        .vertex_stride = 3U * sizeof(float),
        .vertex_format = VertexFormat::float32x3,
        .index_buffer = indices.get(),
        .index_offset = 0U,
        .index_count = 3U,
        .index_format = IndexFormat::uint32,
        .opaque = true,
    };
    const std::array geometries{geometry};
    const AccelerationStructureBuildDesc blas_desc{
        .type = AccelerationStructureType::bottom_level,
        .flags = AccelerationStructureBuildFlags::prefer_fast_trace,
        .geometries = geometries,
        .instances = {},
    };
    AccelerationStructureBuildSizes blas_sizes{};
    CHECK(query_acceleration_structure_build_sizes(*device, blas_desc, blas_sizes));
    auto blas = create_acceleration_structure(*device, blas_desc, blas_sizes);
    CHECK(blas != nullptr);

    const AccelerationStructureInstanceDesc instance{
        .bottom_level = blas.get(),
        .transform = {
            1.0F, 0.0F, 0.0F, 0.0F,
            0.0F, 1.0F, 0.0F, 0.0F,
            0.0F, 0.0F, 1.0F, 0.0F,
        },
        .instance_id = 1U,
        .mask = 0xFFU,
    };
    const std::array instances{instance};
    const AccelerationStructureBuildDesc tlas_desc{
        .type = AccelerationStructureType::top_level,
        .flags = AccelerationStructureBuildFlags::prefer_fast_trace,
        .geometries = {},
        .instances = instances,
    };
    AccelerationStructureBuildSizes tlas_sizes{};
    CHECK(query_acceleration_structure_build_sizes(*device, tlas_desc, tlas_sizes));
    auto tlas = create_acceleration_structure(*device, tlas_desc, tlas_sizes);
    CHECK(tlas != nullptr);

    auto cooked_shader = load_cooked_shader(*device, VICE_INLINE_RAY_QUERY_MANIFEST);
    CHECK(cooked_shader);
    CHECK(cooked_shader.compute_thread_group_size.has_value());
    auto pipeline = device->create_compute_pipeline(ComputePipelineDesc{
        .compute_shader = cooked_shader.module.get(),
        .thread_group_size = *cooked_shader.compute_thread_group_size,
        .debug_name = "inline ray query smoke pipeline",
    });
    CHECK(pipeline != nullptr);

    auto output = device->create_buffer(BufferDesc{
        .size = sizeof(std::uint32_t),
        .usage = BufferUsage::storage | BufferUsage::copy_source,
        .memory = MemoryLocation::gpu_only,
        .debug_name = "inline ray query output",
    });
    auto readback = device->create_buffer(BufferDesc{
        .size = sizeof(std::uint32_t),
        .usage = BufferUsage::copy_destination,
        .memory = MemoryLocation::gpu_to_cpu,
        .debug_name = "inline ray query readback",
    });
    CHECK(output != nullptr);
    CHECK(readback != nullptr);

    auto commands = device->create_command_list();
    CHECK(commands != nullptr);
    CHECK(commands->copy_buffer(*vertex_upload, 0U, *vertices, 0U, vertex_bytes));
    CHECK(commands->copy_buffer(*index_upload, 0U, *indices, 0U, index_bytes));
    CHECK(build_acceleration_structure(*commands, *blas, blas_desc));
    CHECK(build_acceleration_structure(*commands, *tlas, tlas_desc));
    CHECK(commands->set_compute_pipeline(*pipeline));
    CHECK(commands->set_compute_acceleration_structure(
        ShaderBindingKey{
            .register_class = ShaderRegisterClass::shader_resource,
            .binding = 0U,
            .space = 0U,
            .array_element = 0U,
        },
        *tlas
    ));
    CHECK(commands->set_compute_storage_buffer(
        ShaderBindingKey{
            .register_class = ShaderRegisterClass::unordered_access,
            .binding = 0U,
            .space = 0U,
            .array_element = 0U,
        },
        *output,
        BufferViewDesc{
            .offset = 0U,
            .size = sizeof(std::uint32_t),
            .structure_stride = 0U,
        }
    ));
    CHECK(commands->dispatch(ComputeDispatchDesc{
        .group_count_x = 1U,
        .group_count_y = 1U,
        .group_count_z = 1U,
    }));
    CHECK(commands->copy_buffer(
        *output,
        0U,
        *readback,
        0U,
        sizeof(std::uint32_t)
    ));

    const SubmissionId submission = device->submit(std::move(commands));
    CHECK(submission != invalid_submission);
    CHECK(device->wait_for_submission(submission));

    std::uint32_t hit = 0U;
    CHECK(readback->read(0U, std::as_writable_bytes(std::span{&hit, 1U})));
    CHECK(hit == 1U);
    CHECK(device->wait_idle());
    return 0;
#endif
}

#undef CHECK
