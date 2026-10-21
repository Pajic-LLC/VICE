#include <vice/render/shader_asset.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <span>

#define CHECK(condition, message) \
    do { \
        if (!(condition)) { \
            std::cerr << message << '\n'; \
            return __LINE__; \
        } \
    } while (false)

int main(int argc, char** argv) {
#if !defined(__APPLE__)
    (void)argc;
    (void)argv;
    return 0;
#else
    using namespace vice::render;

    CHECK(argc == 2, "Expected one cooked shader manifest path.");

    auto device = Device::create();
    CHECK(device != nullptr, "Could not create the Metal Tier-1 device.");
    CHECK(device->backend() == Backend::metal, "Unbounded cooked smoke did not create Metal.");

    auto loaded = load_cooked_shader(*device, std::filesystem::path{argv[1]});
    CHECK(loaded, loaded.diagnostics);
    CHECK(loaded.module->stage() == ShaderStage::compute, "Cooked shader is not compute.");
    CHECK(loaded.compute_thread_group_size.has_value(), "Cooked shader lost NumThreads metadata.");
    CHECK(
        loaded.compute_thread_group_size->x == 1U &&
        loaded.compute_thread_group_size->y == 1U &&
        loaded.compute_thread_group_size->z == 1U,
        "Cooked shader has unexpected thread-group dimensions."
    );

    bool found_inputs = false;
    bool found_output = false;
    for (const auto& resource : loaded.module->resources()) {
        if (resource.register_class == ShaderRegisterClass::shader_resource &&
            resource.binding == 0U && resource.space == 0U) {
            CHECK(resource.unbounded && resource.count == 0U,
                "t0,space0 is not a true unbounded range.");
            found_inputs = true;
        }
        if (resource.register_class == ShaderRegisterClass::unordered_access &&
            resource.binding == 0U && resource.space == 1U) {
            CHECK(!resource.unbounded && resource.count == 1U,
                "u0,space1 output binding is malformed.");
            found_output = true;
        }
    }
    CHECK(found_inputs, "Cooked shader is missing unbounded t0,space0.");
    CHECK(found_output, "Cooked shader is missing u0,space1.");

    auto pipeline = device->create_compute_pipeline(ComputePipelineDesc{
        .compute_shader = loaded.module.get(),
        .thread_group_size = *loaded.compute_thread_group_size,
        .debug_name = "VICE MSC unbounded descriptor smoke",
    });
    CHECK(pipeline != nullptr, "Could not create compute pipeline from converted metallib.");

    constexpr std::array<std::uint32_t, 3U> input_values{
        0x11111111U,
        0x22222222U,
        0xC0DEC0DEU,
    };
    constexpr std::uint64_t element_bytes = sizeof(std::uint32_t);
    constexpr std::uint64_t upload_bytes = input_values.size() * element_bytes;

    auto upload = device->create_buffer(BufferDesc{
        .size = upload_bytes,
        .usage = BufferUsage::copy_source,
        .memory = MemoryLocation::cpu_to_gpu,
        .debug_name = "VICE MSC unbounded upload",
    });
    CHECK(upload != nullptr, "Could not create upload buffer.");
    CHECK(upload->write(0U, std::as_bytes(std::span{input_values})), "Could not populate upload buffer.");

    std::array<std::unique_ptr<Buffer>, 3U> inputs;
    for (std::size_t index = 0U; index < inputs.size(); ++index) {
        inputs[index] = device->create_buffer(BufferDesc{
            .size = element_bytes,
            .usage = BufferUsage::storage | BufferUsage::copy_destination,
            .memory = MemoryLocation::gpu_only,
            .debug_name = "VICE MSC unbounded input",
        });
        CHECK(inputs[index] != nullptr, "Could not create unbounded input buffer.");
    }

    auto output = device->create_buffer(BufferDesc{
        .size = element_bytes,
        .usage = BufferUsage::storage | BufferUsage::copy_source,
        .memory = MemoryLocation::gpu_only,
        .debug_name = "VICE MSC unbounded output",
    });
    CHECK(output != nullptr, "Could not create output buffer.");

    auto readback = device->create_buffer(BufferDesc{
        .size = element_bytes,
        .usage = BufferUsage::copy_destination,
        .memory = MemoryLocation::gpu_to_cpu,
        .debug_name = "VICE MSC unbounded readback",
    });
    CHECK(readback != nullptr, "Could not create readback buffer.");

    auto commands = device->create_command_list();
    CHECK(commands != nullptr, "Could not create command list.");
    for (std::size_t index = 0U; index < inputs.size(); ++index) {
        CHECK(commands->copy_buffer(
            *upload,
            static_cast<std::uint64_t>(index) * element_bytes,
            *inputs[index],
            0U,
            element_bytes
        ), "Could not upload an unbounded input element.");
    }

    CHECK(commands->set_compute_pipeline(*pipeline), "Could not bind converted compute pipeline.");
    const BufferViewDesc structured_uint{
        .offset = 0U,
        .size = element_bytes,
        .structure_stride = sizeof(std::uint32_t),
    };
    for (std::uint32_t index = 0U; index < inputs.size(); ++index) {
        CHECK(commands->set_compute_buffer(
            ShaderBindingKey{
                .register_class = ShaderRegisterClass::shader_resource,
                .binding = 0U,
                .space = 0U,
                .array_element = index,
            },
            *inputs[index],
            structured_uint
        ), "Could not bind an unbounded SRV array element.");
    }
    CHECK(commands->set_compute_storage_buffer(
        ShaderBindingKey{
            .register_class = ShaderRegisterClass::unordered_access,
            .binding = 0U,
            .space = 1U,
            .array_element = 0U,
        },
        *output,
        structured_uint
    ), "Could not bind output UAV.");
    CHECK(commands->dispatch(ComputeDispatchDesc{
        .group_count_x = 1U,
        .group_count_y = 1U,
        .group_count_z = 1U,
    }), "Converted unbounded compute dispatch failed to encode.");
    CHECK(commands->copy_buffer(*output, 0U, *readback, 0U, element_bytes),
        "Could not encode unbounded output readback.");

    const SubmissionId submission = device->submit(std::move(commands));
    CHECK(submission != invalid_submission, "Converted unbounded submission failed.");
    CHECK(device->wait_for_submission(submission), "Converted unbounded submission did not complete.");

    std::uint32_t actual = 0U;
    CHECK(readback->read(0U, std::as_writable_bytes(std::span{&actual, 1U})),
        "Could not read converted unbounded output.");
    CHECK(actual == input_values[2], "GPU did not select unbounded descriptor element 2.");
    CHECK(device->wait_idle(), "Metal device did not reach idle after validation.");

    std::cout << "Metal Shader Converter unbounded descriptor execution verified: 0x"
              << std::hex << actual << '\n';
    return 0;
#endif
}

#undef CHECK
