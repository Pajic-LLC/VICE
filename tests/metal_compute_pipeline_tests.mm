#import "compute_pipeline.hpp"

#import <Metal/Metal.h>

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

#define CHECK(condition) do { if (!(condition)) return __LINE__; } while (false)

namespace {

[[nodiscard]] int run_cooked_unbounded_smoke(const char* manifest_path) {
    using namespace vice::render;

    auto device = Device::create();
    CHECK(device != nullptr);
    CHECK(device->backend() == Backend::metal);

    auto loaded = load_cooked_shader(*device, std::filesystem::path{manifest_path});
    if (!loaded) {
        std::cerr << loaded.diagnostics << '\n';
        return __LINE__;
    }
    CHECK(loaded.module->stage() == ShaderStage::compute);
    CHECK(loaded.compute_thread_group_size.has_value());
    CHECK(loaded.compute_thread_group_size->x == 1U);
    CHECK(loaded.compute_thread_group_size->y == 1U);
    CHECK(loaded.compute_thread_group_size->z == 1U);

    bool found_inputs = false;
    bool found_output = false;
    for (const auto& resource : loaded.module->resources()) {
        if (resource.register_class == ShaderRegisterClass::shader_resource &&
            resource.binding == 0U && resource.space == 0U) {
            CHECK(resource.unbounded);
            CHECK(resource.count == 0U);
            found_inputs = true;
        }
        if (resource.register_class == ShaderRegisterClass::unordered_access &&
            resource.binding == 0U && resource.space == 1U) {
            CHECK(!resource.unbounded);
            CHECK(resource.count == 1U);
            found_output = true;
        }
    }
    CHECK(found_inputs);
    CHECK(found_output);

    auto pipeline = device->create_compute_pipeline(ComputePipelineDesc{
        .compute_shader = loaded.module.get(),
        .thread_group_size = *loaded.compute_thread_group_size,
        .debug_name = "VICE MSC unbounded descriptor smoke",
    });
    CHECK(pipeline != nullptr);

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
    CHECK(upload != nullptr);
    CHECK(upload->write(0U, std::as_bytes(std::span{input_values})));

    std::array<std::unique_ptr<Buffer>, 3U> inputs;
    for (std::size_t index = 0U; index < inputs.size(); ++index) {
        inputs[index] = device->create_buffer(BufferDesc{
            .size = element_bytes,
            .usage = BufferUsage::storage | BufferUsage::copy_destination,
            .memory = MemoryLocation::gpu_only,
            .debug_name = "VICE MSC unbounded input",
        });
        CHECK(inputs[index] != nullptr);
    }

    auto output = device->create_buffer(BufferDesc{
        .size = element_bytes,
        .usage = BufferUsage::storage | BufferUsage::copy_source,
        .memory = MemoryLocation::gpu_only,
        .debug_name = "VICE MSC unbounded output",
    });
    CHECK(output != nullptr);

    auto readback = device->create_buffer(BufferDesc{
        .size = element_bytes,
        .usage = BufferUsage::copy_destination,
        .memory = MemoryLocation::gpu_to_cpu,
        .debug_name = "VICE MSC unbounded readback",
    });
    CHECK(readback != nullptr);

    auto commands = device->create_command_list();
    CHECK(commands != nullptr);
    for (std::size_t index = 0U; index < inputs.size(); ++index) {
        CHECK(commands->copy_buffer(
            *upload,
            static_cast<std::uint64_t>(index) * element_bytes,
            *inputs[index],
            0U,
            element_bytes
        ));
    }

    CHECK(commands->set_compute_pipeline(*pipeline));
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
        ));
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
    ));
    CHECK(commands->dispatch(ComputeDispatchDesc{
        .group_count_x = 1U,
        .group_count_y = 1U,
        .group_count_z = 1U,
    }));
    CHECK(commands->copy_buffer(*output, 0U, *readback, 0U, element_bytes));

    const SubmissionId submission = device->submit(std::move(commands));
    CHECK(submission != invalid_submission);
    CHECK(device->wait_for_submission(submission));

    std::uint32_t actual = 0U;
    CHECK(readback->read(0U, std::as_writable_bytes(std::span{&actual, 1U})));
    CHECK(actual == input_values[2]);
    CHECK(device->wait_idle());

    std::cout << "Official Metal Shader Converter unbounded descriptor execution verified: 0x"
              << std::hex << actual << '\n';
    return 0;
}

} // namespace

int main() {
    @autoreleasepool {
        using namespace vice::render;
        using namespace vice::render::metal_detail;

        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        CHECK(device != nil);

        constexpr std::string_view source_text = R"MSL(
#include <metal_stdlib>
using namespace metal;

kernel void vice_compute_test(
    device uint* output [[buffer(0)]],
    uint index [[thread_position_in_grid]])
{
    output[index] = 0xBEEF0000u + index;
}
)MSL";
        NSString* source = [[NSString alloc]
            initWithBytes:source_text.data()
            length:source_text.size()
            encoding:NSUTF8StringEncoding];
        CHECK(source != nil);

        NSError* error = nil;
        id<MTLLibrary> library = [device newLibraryWithSource:source options:nil error:&error];
        CHECK(library != nil);
        id<MTLFunction> function = [library newFunctionWithName:@"vice_compute_test"];
        CHECK(function != nil);

        constexpr std::array resources{
            ShaderResourceBindingDesc{
                .register_class = ShaderRegisterClass::unordered_access,
                .binding = 0U,
                .space = 0U,
                .count = 1U,
                .unbounded = false,
            },
        };
        constexpr ComputeThreadGroupSize group_size{
            .x = 4U,
            .y = 1U,
            .z = 1U,
        };

        const ComputePipelineBuildResult pipeline = build_compute_pipeline(
            device,
            function,
            resources,
            {},
            group_size
        );
        CHECK(pipeline.success);
        CHECK(pipeline.pipeline_state != nil);
        CHECK(pipeline.layout.valid);
        CHECK(pipeline.layout.resources.size() == 1U);
        CHECK(pipeline.thread_group_size.x == 4U);
        CHECK(pipeline.thread_group_size.y == 1U);
        CHECK(pipeline.thread_group_size.z == 1U);
        CHECK(pipeline.diagnostics.empty());

        constexpr std::array unbounded_resources{
            ShaderResourceBindingDesc{
                .register_class = ShaderRegisterClass::unordered_access,
                .binding = 0U,
                .space = 0U,
                .count = 0U,
                .unbounded = true,
            },
        };
        constexpr std::array explicit_locations{
            MetalShaderResourceLocationDesc{
                .register_class = ShaderRegisterClass::unordered_access,
                .binding = 0U,
                .space = 0U,
                .argument_buffer_offset = 0U,
                .size = sizeof(std::uint64_t),
                .model = MetalShaderResourceBindingModel::descriptor_table,
            },
        };
        const ComputePipelineBuildResult explicit_pipeline = build_compute_pipeline(
            device,
            function,
            unbounded_resources,
            explicit_locations,
            group_size
        );
        CHECK(explicit_pipeline.success);
        CHECK(explicit_pipeline.layout.valid);
        CHECK(explicit_pipeline.layout.resources.size() == 1U);
        CHECK(explicit_pipeline.layout.resources[0].unbounded);
        CHECK(explicit_pipeline.locations.size() == 1U);
        CHECK(explicit_pipeline.locations[0].model == MetalShaderResourceBindingModel::descriptor_table);

        constexpr NSUInteger element_count = 4U;
        constexpr NSUInteger byte_count = element_count * sizeof(std::uint32_t);
        id<MTLBuffer> output = [device newBufferWithLength:byte_count options:MTLResourceStorageModePrivate];
        id<MTLBuffer> readback = [device newBufferWithLength:byte_count options:MTLResourceStorageModeShared];
        CHECK(output != nil);
        CHECK(readback != nil);

        id<MTLCommandQueue> queue = [device newCommandQueue];
        CHECK(queue != nil);
        id<MTLCommandBuffer> commands = [queue commandBuffer];
        CHECK(commands != nil);
        id<MTLComputeCommandEncoder> compute = [commands computeCommandEncoder];
        CHECK(compute != nil);
        [compute setComputePipelineState:pipeline.pipeline_state];
        [compute setBuffer:output offset:0U atIndex:0U];
        [compute useResource:output usage:MTLResourceUsageWrite];
        [compute dispatchThreadgroups:MTLSizeMake(1U, 1U, 1U)
                 threadsPerThreadgroup:MTLSizeMake(
                     pipeline.thread_group_size.x,
                     pipeline.thread_group_size.y,
                     pipeline.thread_group_size.z
                 )];
        [compute endEncoding];

        id<MTLBlitCommandEncoder> blit = [commands blitCommandEncoder];
        CHECK(blit != nil);
        [blit copyFromBuffer:output
                sourceOffset:0U
                    toBuffer:readback
           destinationOffset:0U
                        size:byte_count];
        [blit endEncoding];

        [commands commit];
        [commands waitUntilCompleted];
        CHECK(commands.status == MTLCommandBufferStatusCompleted);

        const auto* values = static_cast<const std::uint32_t*>(readback.contents);
        CHECK(values != nullptr);
        for (std::uint32_t index = 0U; index < element_count; ++index) {
            CHECK(values[index] == 0xBEEF0000U + index);
        }

        const ComputePipelineBuildResult invalid = build_compute_pipeline(
            nil,
            function,
            resources,
            {},
            group_size
        );
        CHECK(!invalid.success);
        CHECK(!invalid.diagnostics.empty());

        if (const char* manifest = std::getenv("VICE_METAL_UNBOUNDED_SMOKE_MANIFEST");
            manifest != nullptr && manifest[0] != '\0') {
            const int result = run_cooked_unbounded_smoke(manifest);
            if (result != 0) return result;
        }

        return 0;
    }
}

#undef CHECK
