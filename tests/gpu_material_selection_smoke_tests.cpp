#include <vice/render/shader_asset.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <span>
#include <string_view>
#include <utility>

#define CHECK(condition) do { \
    if (!(condition)) { \
        std::cerr << "CHECK failed: " #condition " at " << __FILE__ << ':' << __LINE__ << '\n'; \
        return __LINE__; \
    } \
} while (false)

namespace {

struct SmokeMaterialRecord {
    std::uint32_t texture_index = 0U;
    std::uint32_t sampler_index = 0U;
    std::uint32_t reserved[2]{};
};

static_assert(sizeof(SmokeMaterialRecord) == 16U);

[[nodiscard]] std::unique_ptr<vice::render::Buffer> create_gpu_input(
    vice::render::Device& device,
    vice::render::CommandList& commands,
    const std::span<const std::byte> bytes,
    const std::string_view debug_name
) {
    using namespace vice::render;

    auto upload = device.create_buffer(BufferDesc{
        .size = bytes.size(),
        .usage = BufferUsage::copy_source,
        .memory = MemoryLocation::cpu_to_gpu,
        .debug_name = "VICE GPU material selection smoke upload",
    });
    if (upload == nullptr || !upload->write(0U, bytes)) return nullptr;

    auto gpu = device.create_buffer(BufferDesc{
        .size = bytes.size(),
        .usage = BufferUsage::storage | BufferUsage::copy_destination,
        .memory = MemoryLocation::gpu_only,
        .debug_name = debug_name,
    });
    if (gpu == nullptr || !commands.copy_buffer(*upload, 0U, *gpu, 0U, bytes.size())) {
        return nullptr;
    }
    return gpu;
}

[[nodiscard]] constexpr std::uint32_t pack_rgba(
    const std::uint32_t red,
    const std::uint32_t green,
    const std::uint32_t blue,
    const std::uint32_t alpha
) noexcept {
    return red | (green << 8U) | (blue << 16U) | (alpha << 24U);
}

} // namespace

int main() {
    using namespace vice::render;

    const char* const manifest = std::getenv("VICE_GPU_MATERIAL_SELECTION_SMOKE_MANIFEST");
    if (manifest == nullptr || manifest[0] == '\0') {
        return 0;
    }

    auto device = Device::create();
    CHECK(device != nullptr);

    auto loaded = load_cooked_shader(*device, std::filesystem::path{manifest});
    if (!loaded) {
        std::cerr << loaded.diagnostics << '\n';
        return __LINE__;
    }
    CHECK(loaded.module->stage() == ShaderStage::compute);
    CHECK(loaded.compute_thread_group_size.has_value());
    CHECK(loaded.compute_thread_group_size->x == 2U);
    CHECK(loaded.compute_thread_group_size->y == 1U);
    CHECK(loaded.compute_thread_group_size->z == 1U);

    bool unbounded_textures = false;
    bool unbounded_samplers = false;
    bool output_uav = false;
    for (const auto& resource : loaded.module->resources()) {
        if (resource.register_class == ShaderRegisterClass::shader_resource &&
            resource.binding == 0U && resource.space == 1U) {
            CHECK(resource.count == 0U);
            CHECK(resource.unbounded);
            unbounded_textures = true;
        }
        if (resource.register_class == ShaderRegisterClass::sampler &&
            resource.binding == 0U && resource.space == 1U) {
            CHECK(resource.count == 0U);
            CHECK(resource.unbounded);
            unbounded_samplers = true;
        }
        if (resource.register_class == ShaderRegisterClass::unordered_access &&
            resource.binding == 0U && resource.space == 2U) {
            CHECK(resource.count == 1U);
            CHECK(!resource.unbounded);
            output_uav = true;
        }
    }
    CHECK(unbounded_textures);
    CHECK(unbounded_samplers);
    CHECK(output_uav);

    auto pipeline = device->create_compute_pipeline(ComputePipelineDesc{
        .compute_shader = loaded.module.get(),
        .thread_group_size = *loaded.compute_thread_group_size,
        .debug_name = "VICE GPU material selection smoke pipeline",
    });
    CHECK(pipeline != nullptr);

    TextureDesc texture_desc;
    texture_desc.dimension = TextureDimension::d2;
    texture_desc.width = 1U;
    texture_desc.height = 1U;
    texture_desc.depth = 1U;
    texture_desc.mip_levels = 1U;
    texture_desc.array_layers = 1U;
    texture_desc.sample_count = 1U;
    texture_desc.format = TextureFormat::rgba8_unorm;
    texture_desc.usage = TextureUsage::sampled | TextureUsage::copy_destination;
    texture_desc.debug_name = "VICE GPU material selection smoke texture";

    auto texture_a = device->create_texture(texture_desc);
    auto texture_b = device->create_texture(texture_desc);
    CHECK(texture_a != nullptr);
    CHECK(texture_b != nullptr);

    constexpr std::array texture_a_pixel{
        std::byte{0x11U}, std::byte{0x22U}, std::byte{0x33U}, std::byte{0xFFU},
    };
    constexpr std::array texture_b_pixel{
        std::byte{0xA1U}, std::byte{0xB2U}, std::byte{0xC3U}, std::byte{0xFFU},
    };
    CHECK(texture_a->write_2d(texture_a_pixel, 4U));
    CHECK(texture_b->write_2d(texture_b_pixel, 4U));

    SamplerDesc sampler_desc;
    sampler_desc.min_filter = Filter::nearest;
    sampler_desc.mag_filter = Filter::nearest;
    sampler_desc.mip_filter = Filter::nearest;
    sampler_desc.address_u = AddressMode::clamp_to_edge;
    sampler_desc.address_v = AddressMode::clamp_to_edge;
    sampler_desc.address_w = AddressMode::clamp_to_edge;
    sampler_desc.max_anisotropy = 1U;
    sampler_desc.debug_name = "VICE GPU material selection smoke sampler";
    auto sampler = device->create_sampler(sampler_desc);
    CHECK(sampler != nullptr);

    constexpr std::array<std::uint32_t, 2U> visible_scene_instances{0U, 1U};
    constexpr std::array<std::array<std::uint32_t, 4U>, 2U> instance_materials{{
        {1U, 0U, 0U, 0U},
        {0U, 0U, 0U, 0U},
    }};
    constexpr std::array<SmokeMaterialRecord, 2U> material_records{{
        {.texture_index = 0U, .sampler_index = 0U},
        {.texture_index = 1U, .sampler_index = 0U},
    }};

    auto commands = device->create_command_list();
    CHECK(commands != nullptr);

    auto visible_buffer = create_gpu_input(
        *device,
        *commands,
        std::as_bytes(std::span{visible_scene_instances}),
        "VICE GPU material selection visible instances"
    );
    auto instance_material_buffer = create_gpu_input(
        *device,
        *commands,
        std::as_bytes(std::span{instance_materials}),
        "VICE GPU material selection instance materials"
    );
    auto material_buffer = create_gpu_input(
        *device,
        *commands,
        std::as_bytes(std::span{material_records}),
        "VICE GPU material selection material records"
    );
    CHECK(visible_buffer != nullptr);
    CHECK(instance_material_buffer != nullptr);
    CHECK(material_buffer != nullptr);

    constexpr std::uint64_t output_bytes = 2U * sizeof(std::uint32_t);
    auto output = device->create_buffer(BufferDesc{
        .size = output_bytes,
        .usage = BufferUsage::storage | BufferUsage::copy_source,
        .memory = MemoryLocation::gpu_only,
        .debug_name = "VICE GPU material selection output",
    });
    auto readback = device->create_buffer(BufferDesc{
        .size = output_bytes,
        .usage = BufferUsage::copy_destination,
        .memory = MemoryLocation::gpu_to_cpu,
        .debug_name = "VICE GPU material selection readback",
    });
    CHECK(output != nullptr);
    CHECK(readback != nullptr);

    CHECK(commands->set_compute_pipeline(*pipeline));
    CHECK(commands->set_compute_buffer(
        ShaderBindingKey{
            .register_class = ShaderRegisterClass::shader_resource,
            .binding = 0U,
            .space = 0U,
            .array_element = 0U,
        },
        *visible_buffer,
        BufferViewDesc{
            .offset = 0U,
            .size = sizeof(visible_scene_instances),
            .structure_stride = sizeof(std::uint32_t),
        }
    ));
    CHECK(commands->set_compute_buffer(
        ShaderBindingKey{
            .register_class = ShaderRegisterClass::shader_resource,
            .binding = 1U,
            .space = 0U,
            .array_element = 0U,
        },
        *instance_material_buffer,
        BufferViewDesc{
            .offset = 0U,
            .size = sizeof(instance_materials),
            .structure_stride = 4U * sizeof(std::uint32_t),
        }
    ));
    CHECK(commands->set_compute_buffer(
        ShaderBindingKey{
            .register_class = ShaderRegisterClass::shader_resource,
            .binding = 2U,
            .space = 0U,
            .array_element = 0U,
        },
        *material_buffer,
        BufferViewDesc{
            .offset = 0U,
            .size = sizeof(material_records),
            .structure_stride = sizeof(SmokeMaterialRecord),
        }
    ));
    CHECK(commands->set_compute_texture(
        ShaderBindingKey{
            .register_class = ShaderRegisterClass::shader_resource,
            .binding = 0U,
            .space = 1U,
            .array_element = 0U,
        },
        *texture_a
    ));
    CHECK(commands->set_compute_texture(
        ShaderBindingKey{
            .register_class = ShaderRegisterClass::shader_resource,
            .binding = 0U,
            .space = 1U,
            .array_element = 1U,
        },
        *texture_b
    ));
    CHECK(commands->set_compute_sampler(
        ShaderBindingKey{
            .register_class = ShaderRegisterClass::sampler,
            .binding = 0U,
            .space = 1U,
            .array_element = 0U,
        },
        *sampler
    ));
    CHECK(commands->set_compute_storage_buffer(
        ShaderBindingKey{
            .register_class = ShaderRegisterClass::unordered_access,
            .binding = 0U,
            .space = 2U,
            .array_element = 0U,
        },
        *output,
        BufferViewDesc{
            .offset = 0U,
            .size = output_bytes,
            .structure_stride = sizeof(std::uint32_t),
        }
    ));
    CHECK(commands->dispatch(ComputeDispatchDesc{
        .group_count_x = 1U,
        .group_count_y = 1U,
        .group_count_z = 1U,
    }));
    CHECK(commands->copy_buffer(*output, 0U, *readback, 0U, output_bytes));

    const SubmissionId submission = device->submit(std::move(commands));
    CHECK(submission != invalid_submission);
    CHECK(device->wait_for_submission(submission));

    std::array<std::uint32_t, 2U> actual{};
    CHECK(readback->read(0U, std::as_writable_bytes(std::span{actual})));
    CHECK(actual[0] == pack_rgba(0xA1U, 0xB2U, 0xC3U, 0xFFU));
    CHECK(actual[1] == pack_rgba(0x11U, 0x22U, 0x33U, 0xFFU));
    CHECK(device->wait_idle());

    std::cout << "GPU material selection execution verified: 0x"
              << std::hex << actual[0] << ", 0x" << actual[1] << '\n';
    return 0;
}

#undef CHECK
