#include <vice/render/shader_asset.hpp>
#include <vice/runtime/ray_reflection_history.hpp>
#include <vice/runtime/ray_reflections.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
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

[[nodiscard]] bool near(
    const float left,
    const float right,
    const float epsilon = 0.02F
) noexcept {
    return std::isfinite(left) && std::isfinite(right) && std::abs(left - right) <= epsilon;
}

} // namespace

int main() {
#if !defined(VICE_RUNTIME_RAY_REFLECTION_HISTORY_MANIFEST) || \
    !defined(VICE_RUNTIME_RAY_REFLECTION_VALIDATION_READBACK_MANIFEST)
    return 0;
#else
    using namespace vice;
    using namespace vice::render;
    using namespace vice::runtime;

    auto device = Device::create();
    CHECK(device != nullptr);
    if (!device->capabilities().ray_tracing) {
        std::cout << "SKIP: " << backend_name(device->backend())
                  << " device exposes no hardware ray tracing; reflection-history execution skipped.\n";
        return 0;
    }

    RuntimeRayReflectionHistoryPass history_pass;
    std::string diagnostics;
    CHECK(history_pass.initialize(
        *device,
        VICE_RUNTIME_RAY_REFLECTION_HISTORY_MANIFEST,
        &diagnostics
    ));
    CHECK(history_pass.available());
    CHECK(diagnostics.empty());

    auto readback_shader = load_cooked_shader(
        *device,
        VICE_RUNTIME_RAY_REFLECTION_VALIDATION_READBACK_MANIFEST
    );
    CHECK(readback_shader);
    CHECK(readback_shader.compute_thread_group_size.has_value());
    auto readback_pipeline = device->create_compute_pipeline(ComputePipelineDesc{
        .compute_shader = readback_shader.module.get(),
        .thread_group_size = *readback_shader.compute_thread_group_size,
        .debug_name = "reflection history validation readback",
    });
    CHECK(readback_pipeline != nullptr);

    auto radiance = device->create_texture(TextureDesc{
        .width = 1U,
        .height = 1U,
        .format = TextureFormat::rgba16_float,
        .usage = TextureUsage::sampled | TextureUsage::storage | TextureUsage::copy_destination,
        .debug_name = "reflection history validation radiance",
    });
    auto previous_color = device->create_texture(TextureDesc{
        .width = 1U,
        .height = 1U,
        .format = TextureFormat::rgba16_float,
        .usage = TextureUsage::sampled | TextureUsage::copy_destination,
        .debug_name = "reflection history validation previous color",
    });
    auto previous_depth = device->create_texture(TextureDesc{
        .width = 1U,
        .height = 1U,
        .format = TextureFormat::depth32_float,
        .usage = TextureUsage::depth_attachment | TextureUsage::sampled,
        .debug_name = "reflection history validation previous depth",
    });
    CHECK(radiance != nullptr);
    CHECK(previous_color != nullptr);
    CHECK(previous_depth != nullptr);

    // Current raw fallback is deliberately different from history so successful reconstruction is observable.
    constexpr std::array<std::uint16_t, 4> current_radiance{
        0x3400U, 0x3800U, 0x3C00U, 0x3C00U
    };
    // (0.75, 0.25, 0.125, 1.0) in IEEE-754 binary16.
    constexpr std::array<std::uint16_t, 4> previous_radiance{
        0x3A00U, 0x3400U, 0x3000U, 0x3C00U
    };
    CHECK(radiance->write_2d(
        std::as_bytes(std::span{current_radiance}),
        sizeof(current_radiance)
    ));
    CHECK(previous_color->write_2d(
        std::as_bytes(std::span{previous_radiance}),
        sizeof(previous_radiance)
    ));

    constexpr float previous_device_depth = 0.5F;
    auto depth_commands = device->create_command_list();
    CHECK(depth_commands != nullptr);
    CHECK(depth_commands->begin_depth_render_pass(
        *previous_depth,
        previous_device_depth,
        true
    ));
    CHECK(depth_commands->end_render_pass());
    const SubmissionId depth_submission = device->submit(std::move(depth_commands));
    CHECK(depth_submission != invalid_submission);
    CHECK(device->wait_for_submission(depth_submission));

    const RuntimeRayReflectionHit committed_hit{
        .world_position = {0.0F, 0.0F, 0.5F},
        .instance_id_plus_one = 43U,
    };
    auto hit_upload = device->create_buffer(BufferDesc{
        .size = sizeof(committed_hit),
        .usage = BufferUsage::copy_source,
        .memory = MemoryLocation::cpu_to_gpu,
        .debug_name = "reflection history validation hit upload",
    });
    auto hits = device->create_buffer(BufferDesc{
        .size = sizeof(committed_hit),
        .usage = BufferUsage::storage | BufferUsage::copy_destination,
        .memory = MemoryLocation::gpu_only,
        .debug_name = "reflection history validation hits",
    });
    CHECK(hit_upload != nullptr);
    CHECK(hits != nullptr);
    CHECK(hit_upload->write(
        0U,
        std::as_bytes(std::span{&committed_hit, 1U})
    ));

    const world::GlobalPosition previous_origin{1.0e12, -2.0e12, 3.0e12};
    const world::GlobalPosition current_origin{1.0e12, -2.0e12, 3.0e12};
    const auto constants = make_runtime_ray_reflection_history_constants(
        math::identity(),
        1U,
        1U,
        true,
        current_origin,
        previous_origin
    );
    CHECK(constants.has_value());

    auto radiance_probe = device->create_buffer(BufferDesc{
        .size = 4U * sizeof(float),
        .usage = BufferUsage::storage | BufferUsage::copy_source,
        .memory = MemoryLocation::gpu_only,
        .debug_name = "reflection history validation probe",
    });
    auto radiance_readback = device->create_buffer(BufferDesc{
        .size = 4U * sizeof(float),
        .usage = BufferUsage::copy_destination,
        .memory = MemoryLocation::gpu_to_cpu,
        .debug_name = "reflection history validation readback",
    });
    CHECK(radiance_probe != nullptr);
    CHECK(radiance_readback != nullptr);

    auto commands = device->create_command_list();
    CHECK(commands != nullptr);
    CHECK(commands->copy_buffer(
        *hit_upload,
        0U,
        *hits,
        0U,
        sizeof(committed_hit)
    ));
    CHECK(history_pass.dispatch(
        *commands,
        *radiance,
        *hits,
        *previous_color,
        *previous_depth,
        *constants,
        &diagnostics
    ));
    CHECK(diagnostics.empty());

    CHECK(commands->set_compute_pipeline(*readback_pipeline));
    CHECK(commands->set_compute_texture(
        binding(ShaderRegisterClass::shader_resource, 0U),
        *radiance
    ));
    CHECK(commands->set_compute_storage_buffer(
        binding(ShaderRegisterClass::unordered_access, 0U),
        *radiance_probe,
        BufferViewDesc{
            .offset = 0U,
            .size = 4U * sizeof(float),
            .structure_stride = 4U * sizeof(float),
        }
    ));
    CHECK(commands->dispatch(ComputeDispatchDesc{
        .group_count_x = 1U,
        .group_count_y = 1U,
        .group_count_z = 1U,
    }));
    CHECK(commands->copy_buffer(
        *radiance_probe,
        0U,
        *radiance_readback,
        0U,
        4U * sizeof(float)
    ));

    const SubmissionId submission = device->submit(std::move(commands));
    CHECK(submission != invalid_submission);
    CHECK(device->wait_for_submission(submission));

    std::array<float, 4> resolved{};
    CHECK(radiance_readback->read(
        0U,
        std::as_writable_bytes(std::span{resolved})
    ));
    CHECK(near(resolved[0], 0.75F));
    CHECK(near(resolved[1], 0.25F));
    CHECK(near(resolved[2], 0.125F));
    CHECK(near(resolved[3], 1.0F));
    CHECK(device->wait_idle());

    std::cout << "Executed production reflection-history reconstruction on "
              << backend_name(device->backend())
              << " with large-world origin-aware temporal constants.\n";
    return 0;
#endif
}

#undef CHECK
