#include <vice/render/ray_tracing.hpp>
#include <vice/render/shader_asset.hpp>
#include <vice/runtime/ray_reflections.hpp>

#include <algorithm>
#include <array>
#include <cmath>
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

namespace {

[[nodiscard]] vice::render::ShaderBindingKey binding(
    const vice::render::ShaderRegisterClass register_class,
    const std::uint32_t slot
) noexcept {
    return vice::render::ShaderBindingKey{
        .register_class = register_class,
        .binding = slot,
        .space = 0U,
        .array_element = 0U,
    };
}

} // namespace

int main() {
#ifndef VICE_RUNTIME_RAY_REFLECTIONS_MANIFEST
    return 0;
#else
    using namespace vice;
    using namespace vice::render;
    using namespace vice::runtime;

    std::ifstream input{VICE_RUNTIME_RAY_REFLECTIONS_MANIFEST, std::ios::binary};
    CHECK(input.good());
    const std::string json{
        std::istreambuf_iterator<char>{input},
        std::istreambuf_iterator<char>{}
    };
    const CookedShaderManifest manifest = parse_cooked_shader_manifest(json);
    CHECK(manifest.valid);
    CHECK(manifest.stage == ShaderStage::compute);
    CHECK(manifest.entry_point == "vice_ray_reflections_cs");
    CHECK(manifest.compute_thread_group_size.has_value());
    CHECK(manifest.compute_thread_group_size->x > 0U);
    CHECK(manifest.compute_thread_group_size->y > 0U);
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

    auto device = Device::create();
    CHECK(device != nullptr);
    if (!device->capabilities().ray_tracing) {
        std::cout << "SKIP: " << backend_name(device->backend())
                  << " device exposes no hardware ray tracing; production reflection manifest validated.\n";
        return 0;
    }

    auto cooked_shader = load_cooked_shader(*device, VICE_RUNTIME_RAY_REFLECTIONS_MANIFEST);
    CHECK(cooked_shader);
    CHECK(cooked_shader.compute_thread_group_size.has_value());
    auto pipeline = device->create_compute_pipeline(ComputePipelineDesc{
        .compute_shader = cooked_shader.module.get(),
        .thread_group_size = *cooked_shader.compute_thread_group_size,
        .debug_name = "production reflection backend validation",
    });
    CHECK(pipeline != nullptr);

    constexpr std::array<float, 9> triangle_vertices{
        1.0F, -2.0F, -3.0F,
        1.0F,  2.0F, -3.0F,
        1.0F,  0.0F,  1.0F,
    };
    constexpr std::array<std::uint32_t, 3> triangle_indices{0U, 1U, 2U};
    constexpr std::uint64_t vertex_bytes = sizeof(triangle_vertices);
    constexpr std::uint64_t index_bytes = sizeof(triangle_indices);

    auto vertex_upload = device->create_buffer(BufferDesc{
        .size = vertex_bytes,
        .usage = BufferUsage::copy_source,
        .memory = MemoryLocation::cpu_to_gpu,
        .debug_name = "reflection validation vertex upload",
    });
    auto index_upload = device->create_buffer(BufferDesc{
        .size = index_bytes,
        .usage = BufferUsage::copy_source,
        .memory = MemoryLocation::cpu_to_gpu,
        .debug_name = "reflection validation index upload",
    });
    auto vertices = device->create_buffer(BufferDesc{
        .size = vertex_bytes,
        .usage = BufferUsage::vertex | BufferUsage::copy_destination,
        .memory = MemoryLocation::gpu_only,
        .debug_name = "reflection validation vertices",
    });
    auto indices = device->create_buffer(BufferDesc{
        .size = index_bytes,
        .usage = BufferUsage::index | BufferUsage::copy_destination,
        .memory = MemoryLocation::gpu_only,
        .debug_name = "reflection validation indices",
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
        .instance_id = 17U,
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

    constexpr std::uint32_t authoritative_scene_index = 42U;
    auto scene_index_upload = device->create_buffer(BufferDesc{
        .size = sizeof(authoritative_scene_index),
        .usage = BufferUsage::copy_source,
        .memory = MemoryLocation::cpu_to_gpu,
        .debug_name = "reflection validation scene-index upload",
    });
    auto scene_indices = device->create_buffer(BufferDesc{
        .size = sizeof(authoritative_scene_index),
        .usage = BufferUsage::storage | BufferUsage::copy_destination,
        .memory = MemoryLocation::gpu_only,
        .debug_name = "reflection validation scene indices",
    });
    CHECK(scene_index_upload != nullptr);
    CHECK(scene_indices != nullptr);
    CHECK(scene_index_upload->write(
        0U,
        std::as_bytes(std::span{&authoritative_scene_index, 1U})
    ));

    auto camera_depth = device->create_texture(TextureDesc{
        .width = 1U,
        .height = 1U,
        .format = TextureFormat::depth32_float,
        .usage = TextureUsage::sampled | TextureUsage::copy_destination,
        .debug_name = "reflection validation depth",
    });
    auto surface_guide = device->create_texture(TextureDesc{
        .width = 1U,
        .height = 1U,
        .format = TextureFormat::rgba16_float,
        .usage = TextureUsage::sampled | TextureUsage::copy_destination,
        .debug_name = "reflection validation surface guide",
    });
    auto environment = device->create_texture(TextureDesc{
        .width = 1U,
        .height = 1U,
        .format = TextureFormat::rgba16_float,
        .usage = TextureUsage::sampled | TextureUsage::copy_destination,
        .debug_name = "reflection validation environment",
    });
    auto radiance = device->create_texture(TextureDesc{
        .width = 1U,
        .height = 1U,
        .format = TextureFormat::rgba16_float,
        .usage = TextureUsage::sampled | TextureUsage::storage,
        .debug_name = "reflection validation radiance",
    });
    auto sampler = device->create_sampler(SamplerDesc{
        .debug_name = "reflection validation sampler",
    });
    CHECK(camera_depth != nullptr);
    CHECK(surface_guide != nullptr);
    CHECK(environment != nullptr);
    CHECK(radiance != nullptr);
    CHECK(sampler != nullptr);

    constexpr float depth = 9.0F / 9.9F;
    CHECK(camera_depth->write_2d(
        std::as_bytes(std::span{&depth, 1U}),
        sizeof(depth)
    ));

    // Octahedral encoding for normalize(float3(1, 0, 1)): encoded guide xy = (0.75, 0.5).
    constexpr std::array<std::uint16_t, 4> guide_pixel{
        0x3A00U, 0x3800U, 0x3400U, 0x3C00U
    };
    constexpr std::array<std::uint16_t, 4> environment_pixel{
        0x3400U, 0x3800U, 0x3C00U, 0x3C00U
    };
    CHECK(surface_guide->write_2d(
        std::as_bytes(std::span{guide_pixel}),
        sizeof(guide_pixel)
    ));
    CHECK(environment->write_2d(
        std::as_bytes(std::span{environment_pixel}),
        sizeof(environment_pixel)
    ));

    const RuntimeRayReflectionsCamera camera{
        .position = {},
        .forward = {0.0F, 0.0F, -1.0F},
        .up = {0.0F, 1.0F, 0.0F},
        .vertical_fov_radians = 1.0F,
        .aspect_ratio = 1.0F,
        .near_plane = 0.1F,
        .far_plane = 10.0F,
    };
    const auto constants = make_runtime_ray_reflections_constants(
        camera,
        1U,
        1U,
        1.0F,
        0.0F,
        0.0F,
        9.0F,
        0.01F,
        0.85F
    );
    CHECK(constants.has_value());

    auto constant_buffer = device->create_buffer(BufferDesc{
        .size = sizeof(RuntimeRayReflectionsConstants),
        .usage = BufferUsage::uniform,
        .memory = MemoryLocation::cpu_to_gpu,
        .debug_name = "reflection validation constants",
    });
    auto hits = device->create_buffer(BufferDesc{
        .size = sizeof(RuntimeRayReflectionHit),
        .usage = BufferUsage::storage | BufferUsage::copy_source,
        .memory = MemoryLocation::gpu_only,
        .debug_name = "reflection validation hits",
    });
    auto hit_readback = device->create_buffer(BufferDesc{
        .size = sizeof(RuntimeRayReflectionHit),
        .usage = BufferUsage::copy_destination,
        .memory = MemoryLocation::gpu_to_cpu,
        .debug_name = "reflection validation hit readback",
    });
    CHECK(constant_buffer != nullptr);
    CHECK(hits != nullptr);
    CHECK(hit_readback != nullptr);
    CHECK(constant_buffer->write(
        0U,
        std::as_bytes(std::span{&*constants, 1U})
    ));

    auto commands = device->create_command_list();
    CHECK(commands != nullptr);
    CHECK(commands->copy_buffer(*vertex_upload, 0U, *vertices, 0U, vertex_bytes));
    CHECK(commands->copy_buffer(*index_upload, 0U, *indices, 0U, index_bytes));
    CHECK(commands->copy_buffer(
        *scene_index_upload,
        0U,
        *scene_indices,
        0U,
        sizeof(authoritative_scene_index)
    ));
    CHECK(build_acceleration_structure(*commands, *blas, blas_desc));
    CHECK(build_acceleration_structure(*commands, *tlas, tlas_desc));
    CHECK(commands->set_compute_pipeline(*pipeline));
    CHECK(commands->set_compute_constant_buffer(
        binding(ShaderRegisterClass::constant_buffer, 0U),
        *constant_buffer,
        0U,
        sizeof(RuntimeRayReflectionsConstants)
    ));
    CHECK(commands->set_compute_acceleration_structure(
        binding(ShaderRegisterClass::shader_resource, 0U),
        *tlas
    ));
    CHECK(commands->set_compute_texture(
        binding(ShaderRegisterClass::shader_resource, 1U),
        *camera_depth
    ));
    CHECK(commands->set_compute_texture(
        binding(ShaderRegisterClass::shader_resource, 2U),
        *surface_guide
    ));
    CHECK(commands->set_compute_texture(
        binding(ShaderRegisterClass::shader_resource, 3U),
        *environment
    ));
    CHECK(commands->set_compute_buffer(
        binding(ShaderRegisterClass::shader_resource, 4U),
        *scene_indices,
        BufferViewDesc{
            .offset = 0U,
            .size = sizeof(authoritative_scene_index),
            .structure_stride = sizeof(std::uint32_t),
        }
    ));
    CHECK(commands->set_compute_sampler(
        binding(ShaderRegisterClass::sampler, 0U),
        *sampler
    ));
    CHECK(commands->set_compute_storage_texture(
        binding(ShaderRegisterClass::unordered_access, 0U),
        *radiance
    ));
    CHECK(commands->set_compute_storage_buffer(
        binding(ShaderRegisterClass::unordered_access, 1U),
        *hits,
        BufferViewDesc{
            .offset = 0U,
            .size = sizeof(RuntimeRayReflectionHit),
            .structure_stride = sizeof(RuntimeRayReflectionHit),
        }
    ));
    CHECK(commands->dispatch(ComputeDispatchDesc{
        .group_count_x = 1U,
        .group_count_y = 1U,
        .group_count_z = 1U,
    }));
    CHECK(commands->copy_buffer(
        *hits,
        0U,
        *hit_readback,
        0U,
        sizeof(RuntimeRayReflectionHit)
    ));

    const SubmissionId submission = device->submit(std::move(commands));
    CHECK(submission != invalid_submission);
    CHECK(device->wait_for_submission(submission));

    RuntimeRayReflectionHit hit{};
    CHECK(hit_readback->read(
        0U,
        std::as_writable_bytes(std::span{&hit, 1U})
    ));
    CHECK(hit.instance_id_plus_one == authoritative_scene_index + 1U);
    CHECK(std::isfinite(hit.world_position[0]));
    CHECK(std::isfinite(hit.world_position[1]));
    CHECK(std::isfinite(hit.world_position[2]));
    CHECK(hit.world_position[0] > 0.9F && hit.world_position[0] < 1.1F);
    CHECK(std::abs(hit.world_position[1]) < 0.1F);
    CHECK(hit.world_position[2] < -0.8F && hit.world_position[2] > -1.2F);
    CHECK(radiance->format() == TextureFormat::rgba16_float);
    CHECK(has_usage(radiance->usage(), TextureUsage::storage));
    CHECK(device->wait_idle());

    std::cout << "Executed production reflection shader on "
              << backend_name(device->backend())
              << " with authoritative scene index " << authoritative_scene_index << ".\n";
    return 0;
#endif
}

#undef CHECK
