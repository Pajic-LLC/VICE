#include <vice/render/rhi.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#define CHECK(condition) do { if (!(condition)) return __LINE__; } while (false)

int main() {
    using namespace vice::render;

    auto device = Device::create();
    CHECK(device != nullptr);

    constexpr std::array resources{
        ShaderResourceBindingDesc{
            .register_class = ShaderRegisterClass::unordered_access,
            .binding = 0U,
            .space = 0U,
            .count = 1U,
            .unbounded = false,
        },
    };

#if defined(_WIN32)
    constexpr std::string_view shader_source = R"HLSL(
RWStructuredBuffer<uint> OutputBuffer : register(u0);

[numthreads(4, 1, 1)]
void main(uint3 dispatch_id : SV_DispatchThreadID) {
    OutputBuffer[dispatch_id.x] = 0xC0DE0000u + dispatch_id.x;
}
)HLSL";
    constexpr ShaderSourceLanguage shader_language = ShaderSourceLanguage::hlsl;
#elif defined(__APPLE__)
    constexpr std::string_view shader_source = R"MSL(
#include <metal_stdlib>
using namespace metal;

kernel void main0(
    device uint* output [[buffer(0)]],
    uint index [[thread_position_in_grid]])
{
    output[index] = 0xC0DE0000u + index;
}
)MSL";
    constexpr ShaderSourceLanguage shader_language = ShaderSourceLanguage::metal;
#else
    return 0;
#endif

    const ShaderModuleDesc shader_desc{
        .stage = ShaderStage::compute,
        .language = shader_language,
        .source = shader_source,
        .resources = resources,
#if defined(__APPLE__)
        .entry_point = "main0",
#else
        .entry_point = "main",
#endif
        .debug_name = "VICE public compute smoke",
    };
    auto shader = device->create_shader_module(shader_desc);
    CHECK(shader != nullptr);

    const ComputePipelineDesc pipeline_desc{
        .compute_shader = shader.get(),
        .thread_group_size = ComputeThreadGroupSize{4U, 1U, 1U},
        .debug_name = "VICE public compute smoke pipeline",
    };
    auto pipeline = device->create_compute_pipeline(pipeline_desc);
    CHECK(pipeline != nullptr);

    constexpr std::uint64_t byte_count = 4U * sizeof(std::uint32_t);
    auto output = device->create_buffer(BufferDesc{
        .size = byte_count,
        .usage = BufferUsage::storage | BufferUsage::copy_source,
        .memory = MemoryLocation::gpu_only,
        .debug_name = "VICE public compute smoke output",
    });
    CHECK(output != nullptr);

    auto readback = device->create_buffer(BufferDesc{
        .size = byte_count,
        .usage = BufferUsage::copy_destination,
        .memory = MemoryLocation::gpu_to_cpu,
        .debug_name = "VICE public compute smoke readback",
    });
    CHECK(readback != nullptr);

    auto commands = device->create_command_list();
    CHECK(commands != nullptr);
    CHECK(commands->set_compute_pipeline(*pipeline));
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
            .size = byte_count,
            .structure_stride = sizeof(std::uint32_t),
        }
    ));
    CHECK(commands->dispatch(ComputeDispatchDesc{
        .group_count_x = 1U,
        .group_count_y = 1U,
        .group_count_z = 1U,
    }));
    CHECK(commands->copy_buffer(*output, 0U, *readback, 0U, byte_count));

    const SubmissionId submission = device->submit(std::move(commands));
    CHECK(submission != invalid_submission);
    CHECK(device->wait_for_submission(submission));

    std::array<std::uint32_t, 4> values{};
    CHECK(readback->read(0U, std::as_writable_bytes(std::span{values})));
    for (std::uint32_t index = 0U; index < values.size(); ++index) {
        CHECK(values[index] == 0xC0DE0000U + index);
    }

    CHECK(device->wait_idle());
    return 0;
}

#undef CHECK
