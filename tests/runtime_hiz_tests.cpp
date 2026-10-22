#include <vice/runtime/hiz.hpp>

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <string>

#define CHECK(condition) do { \
    if (!(condition)) { \
        std::cerr << "CHECK failed: " #condition " at line " << __LINE__ << '\n'; \
        return __LINE__; \
    } \
} while (false)

int main() {
    using namespace vice::render;
    using namespace vice::runtime;

    CHECK(runtime_hiz_level_sizes(0U, 8U).empty());
    CHECK(runtime_hiz_level_sizes(8U, 0U).empty());
    CHECK(runtime_hiz_level_sizes(1U, 1U) ==
        std::vector<RuntimeHiZLevelSize>{{1U, 1U}});
    CHECK(runtime_hiz_level_sizes(8U, 8U) ==
        std::vector<RuntimeHiZLevelSize>{{8U, 8U}, {4U, 4U}, {2U, 2U}, {1U, 1U}});
    CHECK(runtime_hiz_level_sizes(7U, 5U) ==
        std::vector<RuntimeHiZLevelSize>{{7U, 5U}, {4U, 3U}, {2U, 2U}, {1U, 1U}});

    auto device = Device::create();
    if (!device) {
        return 0;
    }

    RuntimeHiZPyramid pyramid;
    std::string diagnostics;
    CHECK(pyramid.initialize(*device, &diagnostics));
    CHECK(diagnostics.empty());
    CHECK(pyramid.resize(7U, 5U, &diagnostics));
    CHECK(diagnostics.empty());
    CHECK(pyramid.level_count() == 4U);
    CHECK(pyramid.level_size(0U) == RuntimeHiZLevelSize{7U, 5U});
    CHECK(pyramid.level_size(1U) == RuntimeHiZLevelSize{4U, 3U});
    CHECK(pyramid.level_size(2U) == RuntimeHiZLevelSize{2U, 2U});
    CHECK(pyramid.level_size(3U) == RuntimeHiZLevelSize{1U, 1U});
    CHECK(pyramid.level(4U) == nullptr);

    TextureDesc depth_desc;
    depth_desc.width = 7U;
    depth_desc.height = 5U;
    depth_desc.format = TextureFormat::depth32_float;
    depth_desc.usage = TextureUsage::depth_attachment | TextureUsage::sampled;
    depth_desc.debug_name = "VICE Hi-Z execution source depth";
    auto depth = device->create_texture(depth_desc);
    CHECK(depth != nullptr);

    constexpr std::array probe_resources{
        ShaderResourceBindingDesc{
            .register_class = ShaderRegisterClass::shader_resource,
            .binding = 0U,
            .space = 0U,
            .count = 1U,
            .unbounded = false,
        },
        ShaderResourceBindingDesc{
            .register_class = ShaderRegisterClass::unordered_access,
            .binding = 1U,
            .space = 0U,
            .count = 1U,
            .unbounded = false,
        },
        ShaderResourceBindingDesc{
            .register_class = ShaderRegisterClass::sampler,
            .binding = 0U,
            .space = 0U,
            .count = 1U,
            .unbounded = false,
        },
    };

    ShaderModuleDesc probe_desc;
    probe_desc.stage = ShaderStage::compute;
    probe_desc.resources = probe_resources;
    probe_desc.entry_point = "vice_hiz_probe_cs";
    probe_desc.debug_name = "VICE Hi-Z execution probe";
    if (device->backend() == Backend::metal) {
        probe_desc.language = ShaderSourceLanguage::metal;
        probe_desc.source = R"(
#include <metal_stdlib>
using namespace metal;
kernel void vice_hiz_probe_cs(
    texture2d<float> input_depth [[texture(0)]],
    device uint* output [[buffer(1)]],
    sampler point_sampler [[sampler(0)]]) {
    output[0] = as_type<uint>(input_depth.sample(point_sampler, float2(0.5, 0.5)).r);
}
)";
    } else {
        probe_desc.language = ShaderSourceLanguage::hlsl;
        probe_desc.source = R"(
Texture2D<float> input_depth : register(t0);
RWStructuredBuffer<uint> output : register(u1);
SamplerState point_sampler : register(s0);
[numthreads(1, 1, 1)]
void vice_hiz_probe_cs() {
    output[0] = asuint(input_depth.SampleLevel(point_sampler, float2(0.5, 0.5), 0.0));
}
)";
    }
    auto probe_shader = device->create_shader_module(probe_desc);
    CHECK(probe_shader != nullptr);

    ComputePipelineDesc probe_pipeline_desc;
    probe_pipeline_desc.compute_shader = probe_shader.get();
    probe_pipeline_desc.thread_group_size = {1U, 1U, 1U};
    probe_pipeline_desc.debug_name = "VICE Hi-Z execution probe pipeline";
    auto probe_pipeline = device->create_compute_pipeline(probe_pipeline_desc);
    CHECK(probe_pipeline != nullptr);

    SamplerDesc sampler_desc;
    sampler_desc.min_filter = Filter::nearest;
    sampler_desc.mag_filter = Filter::nearest;
    sampler_desc.mip_filter = Filter::nearest;
    sampler_desc.debug_name = "VICE Hi-Z execution probe sampler";
    auto sampler = device->create_sampler(sampler_desc);
    CHECK(sampler != nullptr);

    BufferDesc output_desc;
    output_desc.size = sizeof(std::uint32_t);
    output_desc.usage = BufferUsage::storage | BufferUsage::copy_source;
    output_desc.memory = MemoryLocation::gpu_only;
    output_desc.debug_name = "VICE Hi-Z execution probe output";
    auto output = device->create_buffer(output_desc);
    CHECK(output != nullptr);

    BufferDesc readback_desc;
    readback_desc.size = sizeof(std::uint32_t);
    readback_desc.usage = BufferUsage::copy_destination;
    readback_desc.memory = MemoryLocation::gpu_to_cpu;
    readback_desc.debug_name = "VICE Hi-Z execution probe readback";
    auto readback = device->create_buffer(readback_desc);
    CHECK(readback != nullptr);

    auto commands = device->create_command_list();
    CHECK(commands != nullptr);
    CHECK(commands->begin_depth_render_pass(*depth, 0.25F, true));
    CHECK(commands->end_render_pass());
    if (!pyramid.build(*commands, *depth, &diagnostics)) {
        std::cerr << diagnostics << '\n';
        return __LINE__;
    }

    Texture* const final_level = pyramid.level(pyramid.level_count() - 1U);
    CHECK(final_level != nullptr);
    CHECK(commands->set_compute_pipeline(*probe_pipeline));
    CHECK(commands->set_compute_texture(
        ShaderBindingKey{
            .register_class = ShaderRegisterClass::shader_resource,
            .binding = 0U,
            .space = 0U,
            .array_element = 0U,
        },
        *final_level
    ));
    CHECK(commands->set_compute_storage_buffer(
        ShaderBindingKey{
            .register_class = ShaderRegisterClass::unordered_access,
            .binding = 1U,
            .space = 0U,
            .array_element = 0U,
        },
        *output,
        BufferViewDesc{
            .offset = 0U,
            .size = sizeof(std::uint32_t),
            .structure_stride = sizeof(std::uint32_t),
        }
    ));
    CHECK(commands->set_compute_sampler(
        ShaderBindingKey{
            .register_class = ShaderRegisterClass::sampler,
            .binding = 0U,
            .space = 0U,
            .array_element = 0U,
        },
        *sampler
    ));
    CHECK(commands->dispatch(ComputeDispatchDesc{}));
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

    std::uint32_t depth_bits = 0U;
    CHECK(readback->read(
        0U,
        std::as_writable_bytes(std::span{&depth_bits, 1})
    ));
    CHECK(depth_bits == 0x3E800000U);
    CHECK(device->wait_idle());

    std::cout << "Hi-Z pyramid execution verified through 7x5 -> 1x1: 0x"
              << std::hex << depth_bits << '\n';
    return 0;
}

#undef CHECK
